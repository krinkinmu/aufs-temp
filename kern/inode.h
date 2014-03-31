#ifndef __INODE_H__
#define __INODE_H__

#include <linux/fs.h>

struct aufs_dinode
{
	__be32 block;
	__be32 length;
	__be32 mode;
	__be32 ino;
	__be32 uid;
	__be32 gid;
	__be64 ctime;
};

struct aufs_inode
{
	uint32_t block;
	uint32_t length;
	struct inode vfs_inode;
};

int aufs_create_inode_cache(void);
void aufs_destroy_inode_cache(void);

struct inode *aufs_alloc_inode(struct super_block *sb);
void aufs_destroy_inode(struct inode *inode);

static inline struct aufs_inode *AUFS_I(struct inode *inode)
{
	return container_of(inode, struct aufs_inode, vfs_inode);
}

#endif /*__INODE_H__*/
