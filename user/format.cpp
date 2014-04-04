#include <stdexcept>
#include <algorithm>
#include <cstring>

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "format.hpp"

namespace {

	void set_bits(size_t from, size_t to, uint8_t *bits)
	{
		size_t const begin = (from + 7) >> 3;
		size_t const end = to >> 3;

		uint8_t const start = ~static_cast<uint8_t>(0xFF >> ((begin << 3) - from));
		uint8_t const finish = ~static_cast<uint8_t>(0xFF << (to - (end << 3)));

		for (size_t it = begin; it < end; ++it)
			*(bits + it) |= static_cast<uint8_t>(0xFF);

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

	void clear_bits(size_t from, size_t to, uint8_t *bits)
	{
		size_t const begin = (from + 7) >> 3;
		size_t const end = to >> 3;

		uint8_t const start = static_cast<uint8_t>(0xFF >> ((begin << 3) - from));
		uint8_t const finish = static_cast<uint8_t>(0xFF << (to - (end << 3)));

		for (size_t it = begin; it < end; ++it)
			*(bits + it) &= static_cast<uint8_t>(0x00);

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

	size_t find_clear(uint8_t *bits, size_t bytes, size_t len)
	{
		size_t const size = ((len + 7) >> 3);
		if (size > bytes)
			return static_cast<size_t>(-1);
		for (size_t it = 0; it <= bytes - size; ++it)
		{
			if ( !std::any_of(bits + it,
							bits + it + size,
							[] (uint8_t num)
							{ return num != 0x00; }) )
				return it << 3;
		}
		return static_cast<size_t>(-1);
	}

	size_t find_clear_bit(uint8_t *bits, size_t bytes)
	{
		uint8_t *it = std::find_if(bits, bits + bytes,
				[](uint8_t byte)
				{ return byte != static_cast<uint8_t>(0xFF); });
		if (static_cast<size_t>(it - bits) == bytes)
			return static_cast<size_t>(-1);
		for (size_t bit = 0; bit != 8; ++bit)
		{
			if ((*it >> bit) % 2 == 0)
				return ((bits - it) << 3) | bit;
		}
		return static_cast<size_t>(-1);
	}

	size_t max_inodes_count(size_t blocks_count, size_t block_size)
	{
		size_t const blocks = blocks_count - 3;
		size_t const in_block = block_size / sizeof(struct inode);
		size_t const iblocks = blocks / (in_block - 1) - 1;

		return iblocks * in_block;
	}

}

uint32_t const Formatter::FS_MAGIC = 0x13131313u;

Formatter::Formatter(BlockCache &cache)
	: Formatter(cache, cache.blocks_count())
{ }

Formatter::Formatter(BlockCache &cache, size_t blocks_count)
	: Formatter(cache, std::min(blocks_count, cache.blocks_count()),
		max_inodes_count(std::min(blocks_count, cache.blocks_count()), cache.block_size()))
{ }

Formatter::Formatter(BlockCache &cache, size_t blocks_count, size_t inodes_count)
	: cache_(&cache)
	, super_page_(cache_->block(0))
	, block_page_(cache_->block(1))
	, inode_page_(cache_->block(2))
	, magic_(FS_MAGIC)
	, blocks_count_(blocks_count)
	, inodes_count_(std::min(inodes_count, max_inodes_count(blocks_count, cache.block_size())))
{ format(); }

uint32_t Formatter::magic() const
{ return magic_; }

uint32_t Formatter::block_size() const
{ return cache_->block_size(); }

uint32_t Formatter::blocks_count() const
{ return blocks_count_; }

uint32_t Formatter::inodes_count() const
{ return inodes_count_; }

struct super_block
{
	uint32_t magic;
	uint32_t block_size;
	uint32_t root_inode;
};

uint32_t Formatter::root_inode() const
{
	struct super_block const * const sbp = reinterpret_cast<struct super_block *>(super_page_->data());
	return ntohl(sbp->root_inode);
}

void Formatter::set_root_inode(uint32_t inode)
{
	struct super_block * const sbp = reinterpret_cast<struct super_block *>(super_page_->data());
	sbp->root_inode = htonl(inode);
}

Inode Formatter::alloc_inode()
{
	size_t const start = find_clear_bit(inode_page_->data(), inode_page_->block_size());
	if (start == static_cast<size_t>(-1))
		return Inode(*cache_, 0);
	set_bits(start, start + 1, inode_page_->data());
	return Inode(*cache_, start);
}

uint32_t Formatter::alloc_blocks(size_t count)
{
	size_t const start = find_clear(block_page_->data(),
			block_page_->block_size(), count);
	if (start == static_cast<size_t>(-1))
		return 0;
	set_bits(start, start + count, block_page_->data());
	return start;
}

Inode Formatter::mkfile(uint32_t length)
{
	uint32_t const blocks = (length + block_size() - 1) / block_size();
	uint32_t const block = alloc_blocks(blocks);
	Inode inode = alloc_inode();

	inode.set_block(block);
	inode.set_blocks(blocks);
	inode.set_mode(inode.mode() | S_IFREG);

	return inode;
}

Inode Formatter::mkdir(uint32_t entries)
{
	uint32_t const blocks = (entries * sizeof(struct dir_entry) + block_size() - 1) / block_size();
	uint32_t const block = alloc_blocks(blocks);
	Inode inode = alloc_inode();

	inode.set_block(block);
	inode.set_blocks(blocks);
	inode.set_mode(inode.mode() | S_IFDIR);

	return inode;
}

void Formatter::free(Inode const &inode)
{ clear_bits(inode.inode(), inode.inode() + 1, inode_page_->data()); }

uint32_t Formatter::write(Inode &inode, uint8_t const *data, uint32_t len)
{
	uint32_t const least = inode.blocks() * block_size() - inode.length();
	uint32_t const block = inode.block() + inode.length() / block_size();
	uint32_t const offset = inode.length() % block_size();
	uint32_t const written = std::min(len, static_cast<uint32_t>(block_size() - offset));

	if (!(inode.mode() & S_IFREG))
		throw std::logic_error("it is not file");

	if (len > least)
		throw std::out_of_range("there is no enough space");

	BlockCache::BlockPtr bp = cache_->block(block);
	std::copy_n(data, written, bp->data() + offset);
	inode.set_length(inode.length() + written);

	return written;
}

void Formatter::add_child(Inode &inode, char const *name, Inode const &child)
{
	uint32_t const in_block = block_size() / sizeof(struct dir_entry);
	uint32_t const entries = inode.blocks() * in_block;
	uint32_t const least = entries - inode.length();
	uint32_t const block = inode.block() + inode.length() / in_block;
	uint32_t const offset = (inode.length() % in_block);

	if (!(inode.mode() & S_IFDIR))
		throw std::logic_error("it is not directory");

	if (!least)
		throw std::out_of_range("there is no enough space");

	BlockCache::BlockPtr bp = cache_->block(block);
	struct dir_entry *const dp = reinterpret_cast<struct dir_entry *>(bp->data()) + offset;
	strncpy(dp->name, name, FS_FILENAME_MAXLEN - 1);
	dp->name[FS_FILENAME_MAXLEN - 1] = '\0';
	dp->inode = htonl(child.inode());
	inode.set_length(inode.length() + 1);
}

void Formatter::format()
{
	uint32_t const in_block = block_size() / sizeof(struct inode);
	uint32_t const busy_blocks = 3 + inodes_count() / in_block;

	set_bits(0, busy_blocks, block_page_->data());
	clear_bits(busy_blocks, blocks_count(), block_page_->data());
	set_bits(blocks_count(), block_size() << 3, block_page_->data());

	set_bits(0, 1, inode_page_->data());
	clear_bits(1, inodes_count(), inode_page_->data());
	set_bits(inodes_count(), block_size() << 3, inode_page_->data());

	struct super_block * const sbp = reinterpret_cast<struct super_block *>(super_page_->data());
	sbp->magic = htonl(magic());
	sbp->block_size = htonl(block_size());
	sbp->root_inode = htonl(root_inode());
}
