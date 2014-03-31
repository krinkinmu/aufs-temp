#include <linux/stat.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>

#define AUFS_DEFAULT_BLOCK_BITS		0x0000000Cu
#define AUFS_DEFAULT_ROOT_INO		0x00000000u
#define AUFS_DEFAULT_BLOCKS_COUNT	0x00000000u
#define AUFS_MAGIC_NUMBER			0x13131313u

static uint32_t block_bits = AUFS_DEFAULT_BLOCK_BITS;
static uint32_t root_ino = AUFS_DEFAULT_ROOT_INO;
static uint32_t blocks_count = AUFS_DEFAULT_BLOCKS_COUNT;

struct aufs_super_block
{
	uint32_t magic;
	uint32_t block_size;
	uint32_t blocks_count;
	uint32_t root_ino;
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

static void fill_bmap(uint8_t *bm)
{
	set_bits(0, 3, bm);
	clear_bits(3, (1 << block_bits) << 3, bm);
	set_bits((1 << block_bits) << 3, blocks_count, bm);
}

static void fill_imap(uint8_t *im)
{
	set_bits(0, 1, im);
	clear_bits(1, (1 << block_bits) << 3, im);
}

static int format(int fd)
{
	uint8_t *data = (uint8_t *)calloc(3 * (1 << block_bits), 1);
	struct aufs_super_block *sb = (struct aufs_super_block *)data;
	uint8_t *bm = data + (1 << block_bits);
	uint8_t *im = data + (1 << (block_bits + 1));
	int ret = 0;

	sb->magic = (uint32_t)htonl(AUFS_MAGIC_NUMBER);
	sb->block_size = (uint32_t)htonl(1 << block_bits);
	sb->blocks_count = (uint32_t)htonl(blocks_count);
	sb->root_ino = (uint32_t)htonl(root_ino);

	fill_bmap(bm);
	fill_imap(im);

	ret = write_block(fd, (uint8_t const *)sb, 3 * (1 << block_bits));
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
