#include <linux/buffer_head.h>
#include <linux/slab.h>

#include "super.h"
#include "inode.h"

#define AUFS_FILENAME_MAXLEN	0x0000001C

struct aufs_dir_entry
{
	char name[AUFS_FILENAME_MAXLEN];
	__be32 inode_no;
};

static struct kmem_cache *aufs_inode_cache;

static uint32_t aufs_find_entry(struct inode *inode, char const *name,
		size_t len)
{
	struct aufs_inode const *const ai = AUFS_I(inode);
	size_t const block_size = AUFS_SB(inode->i_sb)->block_size;
	size_t const in_block = block_size / sizeof(struct aufs_dir_entry);

	size_t block = ai->block;
	size_t end = block + inode->i_blocks;
	size_t entry = 0;

	pr_debug("read blocks from %u to %u\n", (unsigned)block, (unsigned)end);

	for (; (entry != inode->i_size) && (block != end); ++block)
	{
		size_t const slots = (inode->i_size - entry) < in_block ?
					(inode->i_size - entry) : in_block;
		size_t slot = 0;
		struct aufs_dir_entry *dirs = NULL;

		struct buffer_head *bh = sb_bread(inode->i_sb, block);
		if (!bh)
		{
			pr_err("find: cannot read block %u\n", (unsigned)block);
			return 0;
		}

		dirs = (struct aufs_dir_entry *)bh->b_data;
		for (; slot != slots; ++slot)
		{
			struct aufs_dir_entry const *const dir = dirs + slot;
			if (!strncmp(name, dir->name, len))
			{
				brelse(bh);
				return be32_to_cpu(dir->inode_no);
			}
		}
		brelse(bh);
		entry += slot;
	}

	return 0;
}

static struct dentry *aufs_lookup(struct inode *dir, struct dentry *dentry,
		unsigned int flags)
{
	struct inode *inode = NULL;
	uint32_t ino = 0;

	if (dentry->d_name.len <= 0 || dentry->d_name.len >= AUFS_FILENAME_MAXLEN)
		return NULL;

	pr_debug("aufs lookup called for %s\n", dentry->d_name.name);

	ino = aufs_find_entry(dir, dentry->d_name.name, (size_t)dentry->d_name.len);
	if (ino)
		inode = aufs_inode_get(dir->i_sb, ino);

	if (inode)
		d_add(dentry, inode);

	return NULL;
}

static struct inode_operations const aufs_dir_inode_ops = {
	.lookup = aufs_lookup,
};

static int aufs_iterate(struct file *fp, struct dir_context *ctx)
{
	struct inode *inode = file_inode(fp);
	struct aufs_inode *ai = AUFS_I(inode);
	size_t const block_size = AUFS_SB(inode->i_sb)->block_size;
	size_t const in_block = block_size / sizeof(struct aufs_dir_entry);

	size_t block = 0;
	size_t end = 0;
	size_t entry = 0;

	pr_debug("aufs readdir %s\n", (char const *)fp->f_path.dentry->d_name.name);

	if (!dir_emit_dots(fp, ctx))
		return 0;

	if (ctx->pos >= inode->i_size + 2)
		return 0;

	block = ai->block + (ctx->pos - 2) / in_block;
	end = ai->block + inode->i_blocks;
	entry = ctx->pos - 2;

	for (; (entry != inode->i_size) && (block != end); ++block)
	{
		size_t const slots = (inode->i_size - entry) < in_block ?
					(inode->i_size - entry) : in_block;
		size_t slot = entry % in_block;
		struct aufs_dir_entry *dirs = NULL;

		struct buffer_head *bh = sb_bread(inode->i_sb, block);
		if (!bh)
		{
			pr_err("iterate: cannot read block %u\n", (unsigned)block);
			return 0;
		}

		dirs = (struct aufs_dir_entry *)bh->b_data;
		for (; slot != slots; ++slot)
		{
			struct aufs_dir_entry const *const dir = dirs + slot;
			if (!dir_emit(ctx, dir->name, strlen(dir->name),
						be32_to_cpu(dir->inode_no), DT_UNKNOWN))
				break;
		}
		brelse(bh);
		entry += slot;
	}
	ctx->pos = entry + 2;

	return 0;
}

static struct file_operations const aufs_dir_file_ops = {
	.owner = THIS_MODULE,
	.iterate = aufs_iterate,
	.llseek = generic_file_llseek,
	.read = generic_read_dir,
};


ssize_t aufs_read(struct file *fp, char __user *buf, size_t len, loff_t *ppos)
{
	struct inode *inode = file_inode(fp);
	struct aufs_super_block *asb = AUFS_SB(inode->i_sb);
	struct aufs_inode *ai = AUFS_I(inode);
	struct buffer_head *bh = NULL;

	size_t const block = ai->block + *ppos / asb->block_size;
	size_t const offset = *ppos % asb->block_size;

	size_t remain = 0, in_block = 0, count = 0;

	if (*ppos >= inode->i_size)
		return 0;

	remain = inode->i_size - *ppos;
	in_block = remain < (asb->block_size - offset) ?
				remain : asb->block_size - offset;
	count = len < in_block ? len : in_block;

	bh = sb_bread(inode->i_sb, block);
	if (!bh)
	{
		pr_err("cannot read block %u\n", (unsigned)block);
		return -EIO;
	}

	if (copy_to_user(buf, (char const *)bh->b_data + offset, count))
	{
		brelse(bh);
		pr_err("cannot copy buffer_ to userspace\n");
		return -EFAULT;
	}

	brelse(bh);
	*ppos += count;

	return count;
}

static struct file_operations const aufs_file_file_ops = {
	.owner = THIS_MODULE,
	.read = aufs_read,
};

struct inode *aufs_inode_get(struct super_block *sb, uint32_t no)
{
	struct aufs_super_block const *const asb = AUFS_SB(sb);
	uint32_t const in_block = asb->block_size / sizeof(struct aufs_dinode);
	struct buffer_head *bh = NULL;
	struct aufs_dinode *di = NULL;
	struct aufs_inode *ai = NULL;
	struct inode *inode = NULL;

	uint32_t block_no = 0;
	uint32_t block_in = 0;

	inode = iget_locked(sb, no);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	if (!(inode->i_state & I_NEW))
		return inode;

	ai = AUFS_I(inode);
	block_no = 3 + no / in_block;
	block_in = no % in_block;

	pr_debug("read inode block %u, offset = %u\n", (unsigned)block_no, (unsigned)block_in);

	bh = sb_bread(sb, block_no);
	if (!bh)
	{
		pr_err("inode: cannot read block %u\n", (unsigned)block_no);
		goto read_error;
	}

	di = (struct aufs_dinode *)(bh->b_data) + block_in;
	ai->block = be32_to_cpu(di->block);
	inode->i_mode = be32_to_cpu(di->mode);
	inode->i_size = be32_to_cpu(di->length);
	inode->i_blocks = be32_to_cpu(di->blocks);
	inode->i_ctime.tv_sec = (uint32_t)be64_to_cpu(di->ctime);
	inode->i_mtime.tv_sec = inode->i_atime.tv_sec =
			inode->i_ctime.tv_sec;
	inode->i_mtime.tv_nsec = inode->i_atime.tv_nsec =
			inode->i_ctime.tv_nsec = 0;
	i_uid_write(inode, (uid_t)be32_to_cpu(di->uid));
	i_gid_write(inode, (gid_t)be32_to_cpu(di->gid));
	brelse(bh);

	switch (inode->i_mode & S_IFMT)
	{
	case S_IFDIR:
		inode->i_op = &aufs_dir_inode_ops;
		inode->i_fop = &aufs_dir_file_ops;
		break;
	case S_IFREG:
		inode->i_op = &aufs_dir_inode_ops;
		inode->i_fop = &aufs_file_file_ops;
	default:
		pr_err("undefined inode format %x\n",
				(unsigned)inode->i_mode & S_IFMT);
		break;
	}

	pr_debug("inode %u info:\n"
				"\tlength = %u\n"
				"\tblock  = %u\n"
				"\tblocks = %u\n"
				"\tuid    = %u\n"
				"\tgid    = %u\n"
				"\tmode   = %o\n",
				(unsigned)inode->i_ino,
				(unsigned)inode->i_size,
				(unsigned)ai->block,
				(unsigned)inode->i_blocks,
				(unsigned)inode->i_uid,
				(unsigned)inode->i_gid,
				(unsigned)inode->i_mode);

	unlock_new_inode(inode);
	return inode;

read_error:
	pr_err("Cannot read inode %u\n", (unsigned)no);
	iget_failed(inode);
	return ERR_PTR(-EIO);
}

struct inode *aufs_alloc_inode(struct super_block *sb)
{
	struct aufs_inode *const i =
		(struct aufs_inode *)kmem_cache_alloc(aufs_inode_cache, GFP_KERNEL);
	if (!i)
		return NULL;
	i->vfs_inode.i_sb = sb;
	return &i->vfs_inode;
}

static void aufs_destroy_callback(struct rcu_head *head)
{
	struct inode *const inode = container_of(head, struct inode, i_rcu);
	pr_debug("destroing inode %u\n", (unsigned)inode->i_ino);
	kmem_cache_free(aufs_inode_cache, AUFS_I(inode));
}

void aufs_destroy_inode(struct inode *inode)
{
	call_rcu(&inode->i_rcu, aufs_destroy_callback);
}

static void aufs_init_once(void *i)
{
	struct aufs_inode *inode = (struct aufs_inode *)i;
	inode_init_once(&inode->vfs_inode);
}

int aufs_create_inode_cache(void)
{
	aufs_inode_cache = kmem_cache_create("aufs inode cache",
							sizeof(struct aufs_inode),
							0, SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD,
							aufs_init_once);
	if (aufs_inode_cache == NULL)
		return -ENOMEM;
	return 0;
}

void aufs_destroy_inode_cache(void)
{
	rcu_barrier();
	kmem_cache_destroy(aufs_inode_cache);
}
