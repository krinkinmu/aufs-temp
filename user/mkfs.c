#include <linux/stat.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/fs.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <time.h>

#define AUFS_DEFAULT_BLOCK_BITS		0x0000000Cu
#define AUFS_DEFAULT_BLOCKS_COUNT	0x00000000u
#define AUFS_MAGIC_NUMBER			0x13131313u

static uint32_t block_bits = AUFS_DEFAULT_BLOCK_BITS;
static uint32_t blocks_count = AUFS_DEFAULT_BLOCKS_COUNT;

struct aufs_super_block
{
	uint32_t magic;
	uint32_t block_size;
	uint32_t blocks_count;
	uint32_t inodes_count;
	uint32_t start;
	uint32_t root_ino;
};

struct aufs_inode
{
	uint32_t block;
	uint32_t length;
	uint32_t mode;
	uint32_t ino;
	uint32_t uid;
	uint32_t gid;
	uint64_t ctime;
};

static int bdev_size(int fd, uint32_t *size)
{
	uint64_t dev_size = 0;
	int ret = 0;
	assert(size);
	ret = (ioctl(fd, BLKGETSIZE64, &dev_size) == 0);
	if (ret)
		*size = (uint32_t)dev_size;
	return ret;
}

static uint32_t blocks(uint32_t bytes)
{
	uint32_t const block_size = 1 << block_bits;
	uint32_t const max_blocks = block_size << 3;
	uint32_t const blocks = bytes >> block_bits;
	return blocks < max_blocks ? blocks : max_blocks;
}

static int write_block(int fd, uint8_t const *block, size_t size)
{
	size_t written = 0;
	ssize_t ret = -1;

	while (written != size)
	{
		ret = write(fd, block, size - written);
		if (ret < 0)
			return (int)ret;
		written += ret;
	}

	return 0;
}

static int write_block_at(int fd, uint8_t const *block, size_t size, off_t off)
{
	off_t const old = lseek(fd, 0, SEEK_CUR);
	int ret = 0;
	lseek(fd, off, SEEK_SET);
	ret = write_block(fd, block, size);
	lseek(fd, old, SEEK_SET);
	return ret;
}

static void clear_bits(size_t from, size_t to, uint8_t *bits)
{
	size_t const begin = (from + 7) >> 3;
	size_t const end = to >> 3;

	uint8_t const start = (uint8_t)(0xFF >> ((begin << 3) - from));
	uint8_t const finish = (uint8_t)(0xFF << (to - (end << 3)));

	size_t it = begin;
	for (; it < end; ++it)
		*(bits + it) &= (uint8_t)0x00;

	if (begin - 1 == end)
	{
		*(bits + end) &= (start | finish);
		return;
	}

	if ((end << 3) != from)
		*(bits + begin - 1) &= start;

	if ((end << 3) != to)
		*(bits + end) &= finish;
}

static void set_bits(size_t from, size_t to, uint8_t *bits)
{
	size_t const begin = (from + 7) >> 3;
	size_t const end = to >> 3;

	uint8_t const start = ~(uint8_t)(0xFF >> ((begin << 3) - from));
	uint8_t const finish = ~(uint8_t)(0xFF << (to - (end << 3)));

	size_t it = begin;
	for (; it < end; ++it)
		*(bits + it) |= (uint8_t)0xFF;

	if (begin - 1 == end)
	{
		*(bits + end) |= (start & finish);
		return;
	}

	if ((begin << 3) != from)
		*(bits + begin - 1) |= start;

	if ((end << 3) != to)
		*(bits + end) |= finish;
}

static void fill_bmap(struct aufs_super_block *sb, uint8_t *bm)
{
	uint32_t const blk_size = ntohl(sb->block_size);
	uint32_t const blk_count = ntohl(sb->blocks_count);
	uint32_t const start_blk = ntohl(sb->start);

	set_bits(0, start_blk, bm);
	clear_bits(start_blk, blk_count, bm);
	set_bits(blk_count, blk_size << 3, bm);
}

static void fill_imap(struct aufs_super_block *sb, uint8_t *im)
{
	uint32_t const blk_size = ntohl(sb->block_size);
	uint32_t const ino_count = ntohl(sb->inodes_count);

	clear_bits(0, ino_count, im);
	set_bits(ino_count, blk_size << 3, im);
}

static int find_clear(struct aufs_super_block *sb, uint8_t const *bm, size_t *no)
{
	size_t it = 0, bit = 0;

	for (; (it < ntohl(sb->block_size)) && (bm[it] == (uint8_t)0xFF); ++it);

	if (it == ntohl(sb->block_size))
		return 0;

	while ((bm[it] >> bit) % 2 == 1)
		++bit;

	*no = (it << 3) + bit;

	return 1;
}

static int alloc_block(struct aufs_super_block *sb, uint8_t *bm, size_t *no)
{
	int const ret = find_clear(sb, bm, no);
	if (ret)
		set_bits(*no, *no + 1, bm);
	return ret;
}

static int alloc_inode(struct aufs_super_block *sb, uint8_t *im, size_t *no)
{
	int const ret = find_clear(sb, im, no);
	if (ret)
		set_bits(*no, *no + 1, im);
	return ret;
}

static void free_inode(struct aufs_super_block *sb, uint8_t *im, size_t no)
{
	(void)sb;
	clear_bits(no, no + 1, im);
}

static int initialize_root(int fd, struct aufs_super_block *sb,
							uint8_t *bm, uint8_t *im)
{
	size_t root_ino = 0, inode_blk = 0, inode_of = 0, data_block = 0;
	size_t const block_size = 1 << block_bits;
	size_t const ino_in_blk = block_size / sizeof(struct aufs_inode);
	struct aufs_inode *inode = (struct aufs_inode *)calloc(block_size, 1);
	mode_t const max = S_IRWXU | S_IRWXG | S_IRWXO;
	mode_t const old = umask(max);

	int ret = 0;

	umask(old);

	ret = alloc_inode(sb, im, &root_ino);
	if (!ret)
		goto ext;

	ret = alloc_block(sb, bm, &data_block);
	if (!ret)
	{
		free_inode(sb, im, root_ino);
		goto ext;
	}

	inode_blk = 3 + root_ino / ino_in_blk;
	inode_of = root_ino % ino_in_blk;

	sb->root_ino = (uint32_t)htonl(root_ino);
	inode[inode_of].block = (uint32_t)htonl(data_block);
	inode[inode_of].length = (uint32_t)htonl(0);
	inode[inode_of].mode = (uint32_t)htonl(S_IFDIR | (max & ~old));
	inode[inode_of].ino = (uint32_t)htonl(root_ino);
	inode[inode_of].uid = (uint32_t)htonl(getuid());
	inode[inode_of].gid = (uint32_t)htonl(getgid());
	inode[inode_of].ctime = (uint64_t)(htonl(time(NULL)));

	ret = write_block_at(fd, (uint8_t const *)inode, block_size, block_size * inode_blk);

ext:
	free(inode);
	return ret;
}

static int format(int fd)
{
	uint32_t const blk_count = blocks_count;
	uint32_t const blk_size = (1 << block_bits);
	uint32_t const ino_in_blk = blk_size / sizeof(struct aufs_inode);
	uint32_t const iblks = (blk_count - 3)/(ino_in_blk - 1);
	uint32_t const start_blk = 3 + iblks;
	uint32_t const ino_count = ino_in_blk * iblks;
	uint32_t const header_size = 3 * blk_size;

	uint8_t *data = (uint8_t *)calloc(header_size, 1);
	struct aufs_super_block *sb = (struct aufs_super_block *)data;
	uint8_t *bm = data + blk_size;
	uint8_t *im = data + 2 * blk_size;
	int ret = 0;

	sb->magic = (uint32_t)htonl(AUFS_MAGIC_NUMBER);
	sb->block_size = (uint32_t)htonl(blk_size);
	sb->blocks_count = (uint32_t)htonl(blk_count);
	sb->inodes_count = (uint32_t)htonl(ino_count);
	sb->start = (uint32_t)htonl(start_blk);

	fill_bmap(sb, bm);
	fill_imap(sb, im);

	ret = initialize_root(fd, sb, bm, im);
	if (ret == 0)
		write_block_at(fd, (uint8_t const *)sb, header_size, 0);
	else
		printf("err\n");

	free(sb);
	return ret;
}

static void parse_options(int argc, char **argv)
{
	while (argc--)
	{
		char const * const arg = *argv++;
		if ((strcmp(arg, "--block_bits") == 0) || (strcmp(arg, "-bb") == 0))
		{
			unsigned long int bits = 0;
			char *endptr = NULL;
			if (argc)
				bits = strtoul(*argv, &endptr, 10);

			if (bits == 0 || endptr == NULL || *endptr != '\0')
			{
				printf("after %s positive number expected, ignored\n", arg);
				continue;
			}

			block_bits = bits;
			--argc; ++argv;
			continue;
		}

		if ((strcmp(arg, "--blocks_count") == 0) || (strcmp(arg, "--bc") == 0))
		{
			unsigned long int blocks = 0;
			char *endptr = NULL;
			if (argc)
				blocks = strtoul(*argv, &endptr, 10);

			if (blocks < 5 || endptr == NULL || *endptr != '\0')
			{
				printf("after %s number greter than 4 expected, ignored\n", arg);
				continue;
			}

			blocks_count = blocks;
			--argc; ++argv;
			continue;
		}
	}
}

int main(int argc, char **argv)
{
	uint32_t device_blocks = 0;
	uint32_t device_bytes = 0;
	int fd = -1, ret = 0;

	if (argc < 2)
	{
		printf("block device filename expected\n");
		return -1;
	}

	fd = open(argv[1], O_RDWR);
	if (fd < 0)
	{
		perror("cannot open device file: ");
		return -1;
	}

	parse_options(argc - 2, argv + 2);

	if (!bdev_size(fd, &device_bytes))
		printf("cannot detect block device size\n");

	if (device_bytes == 0 && blocks_count == 0)
		return -1;

	device_blocks = blocks(device_bytes);
	if ((blocks_count == 0) || (device_blocks < blocks_count))
		blocks_count = device_blocks;

	ret = format(fd);
	close(fd);

	return ret;
}
