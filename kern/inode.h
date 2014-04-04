#ifndef __INODE_H__
#define __INODE_H__

#include <linux/fs.h>

struct aufs_dinode
{
	__be32 block;
	__be32 blocks;
	__be32 length;
	__be32 uid;
	__be32 gid;
	__be32 mode;
	__be64 ctime;
};

struct aufs_inode
{
	struct inode vfs_inode;
	uint32_t block;
};

struct inode *aufs_inode_get(struct super_block *sb, uint32_t no);

int aufs_create_inode_cache(void);
void aufs_destroy_inode_cache(void);

struct inode *aufs_alloc_inode(struct super_block *sb);
void aufs_destroy_inode(struct inode *inode);

static inline struct aufs_inode *AUFS_I(struct inode *inode)
{
	return (struct aufs_inode *)inode;
}

#endif /*__INODE_H__*/
