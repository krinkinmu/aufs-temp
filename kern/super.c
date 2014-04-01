#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/printk.h>

#include "super.h"
#include "inode.h"

static void aufs_put_super(struct super_block *sb)
{
	struct aufs_super_block *asb = (struct aufs_super_block *)sb->s_fs_info;
	if (asb != NULL)
	{
		if (asb->bmap_bh)
		{
			sync_dirty_buffer(asb->bmap_bh);
			brelse(asb->bmap_bh);
		}
		if (asb->imap_bh)
		{
			sync_dirty_buffer(asb->imap_bh);
			brelse(asb->imap_bh);
		}
		kfree(asb);
	}
	sb->s_fs_info = NULL;
	pr_debug("aufs super block destroyed\n");
}

static struct super_operations const aufs_super_ops = {
	.alloc_inode = aufs_alloc_inode,
	.destroy_inode = aufs_destroy_inode,
	.put_super = aufs_put_super,
};

static struct aufs_super_block *read_super_block(struct super_block *sb)
{
	struct aufs_super_block *asb = (struct aufs_super_block *)
			kzalloc(sizeof(struct aufs_super_block), GFP_NOFS);
	struct aufs_super_block *dsb = NULL;
	struct buffer_head *bh = NULL;

	if (!asb)
	{
		pr_err("cannot allocate super block\n");
		return NULL;
	}

	bh = sb_bread(sb, 0);
	if (!bh)
	{
		pr_err("cannot read 0 block\n");
		goto fre;
	}

	dsb = (struct aufs_super_block *)bh->b_data;
	asb->magic = be32_to_cpu(dsb->magic);
	asb->block_size = be32_to_cpu(dsb->block_size);
	asb->blocks_count = be32_to_cpu(dsb->blocks_count);
	asb->inodes_count = be32_to_cpu(dsb->inodes_count);
	asb->start = be32_to_cpu(dsb->start);
	asb->root_ino = be32_to_cpu(dsb->root_ino);
	brelse(bh);

	if (asb->magic != AUFS_MAGIC_NUMBER)
	{
		pr_err("wrong maigc number %u\n", (unsigned)asb->magic);
		goto fre;
	}

	asb->bmap_bh = sb_bread(sb, 1);
	if (!asb->bmap_bh)
	{
		pr_err("cannot read blocks map\n");
		goto fre;
	}

	asb->imap_bh = sb_bread(sb, 2);
	if (!asb->imap_bh)
	{
		brelse(asb->bmap_bh);
		pr_err("cannot read inodes map\n");
		goto fre;
	}

	asb->bmap = (uint8_t *)asb->bmap_bh->b_data;
	asb->imap = (uint8_t *)asb->imap_bh->b_data;

	pr_debug("aufs superblock info:\n"
				"\tmagic        = %u\n"
				"\tblock_size   = %u\n"
				"\tblocks_count = %u\n"
				"\tinodes_count = %u\n"
				"\tstart        = %u\n"
				"\troot_ino     = %u\n",
				(unsigned)asb->magic,
				(unsigned)asb->block_size,
				(unsigned)asb->blocks_count,
				(unsigned)asb->inodes_count,
				(unsigned)asb->start,
				(unsigned)asb->root_ino);

	return asb;

fre:
	kfree(asb);
	return NULL;
}

static int aufs_fill_sb(struct super_block *sb, void *data, int silent)
{
	struct aufs_super_block *asb = NULL;
	struct inode *root = NULL;

	asb = read_super_block(sb);
	if (!asb)
		return -EINVAL;

	sb->s_magic = asb->magic;
	sb->s_op = &aufs_super_ops;
	sb->s_fs_info = asb;

	if (sb_set_blocksize(sb, asb->block_size) == 0)
	{
		pr_err("device does not support block size %u\n",
					(unsigned)asb->block_size);
		return -EINVAL;
	}

	root = aufs_inode_get(sb, asb->root_ino);
	if (IS_ERR(root))
		return PTR_ERR(root);

	sb->s_root = d_make_root(root);
	if (!sb->s_root)
	{
		pr_err("root creation failed\n");
		return -ENOMEM;
	}

	return 0;
}

static struct dentry *aufs_mount(struct file_system_type *type, int flags,
									char const *dev, void *data)
{
	struct dentry *const entry = mount_bdev(type, flags, dev,
												data, aufs_fill_sb);
	if (IS_ERR(entry))
		pr_err("aufs mounting failed\n");
	else
		pr_debug("aufs mounted\n");
	return entry;
}

static struct file_system_type aufs_type = {
	.owner = THIS_MODULE,
	.name = "aufs",
	.mount = aufs_mount,
	.kill_sb = kill_block_super,
	.fs_flags = FS_REQUIRES_DEV,
};

static int __init aufs_init(void)
{
	int ret = aufs_create_inode_cache();
	if (ret)
	{
		pr_err("cannot create inode cache\n");
		return ret;
	}
	ret = register_filesystem(&aufs_type);
	if (ret)
	{
		aufs_destroy_inode_cache();
		pr_err("cannot register filesystem\n");
		return ret;
	}
	pr_debug("aufs module loaded\n");
	return 0;
}

static void __exit aufs_fini(void)
{
	aufs_destroy_inode_cache();
	if (unregister_filesystem(&aufs_type))
		pr_err("aufs unregistering failed\n");
	pr_debug("aufs module unloaded\n");
}

module_init(aufs_init);
module_exit(aufs_fini);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("kmu");
