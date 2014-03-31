#ifndef __SUPER_H__
#define __SUPER_H__

#define AUFS_MAGIC_NUMBER		0x13131313

struct aufs_super_block
{
	uint32_t magic;
	uint32_t block_size;
	uint32_t blocks_count;
	uint32_t root_ino;
};

#endif /*__SUPER_H__*/
