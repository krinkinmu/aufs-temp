#ifndef __SUPER_H__
#define __SUPER_H__

#include <linux/buffer_head.h>

#define AUFS_MAGIC_NUMBER		0x13131313

struct aufs_super_block
{
	uint32_t magic;
	uint32_t block_size;
	uint32_t blocks_count;
	uint32_t inodes_count;
	uint32_t start;
	uint32_t root_ino;

	struct buffer_head *bmap_bh;
	struct buffer_head *imap_bh;

	uint8_t *bmap;
	uint8_t *imap;
};

static inline struct aufs_super_block *AUFS_SB(struct super_block *sb)
{
	return (struct aufs_super_block *)sb->s_fs_info;
}

#endif /*__SUPER_H__*/
