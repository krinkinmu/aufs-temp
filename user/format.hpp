#ifndef __FORMAT_HPP__
#define __FORMAT_HPP__

#include <cstdint>

#include "cache.hpp"
#include "inode.hpp"

class Formatter
{
public:
	Formatter(BlockCache &cache);
	Formatter(BlockCache &cache, size_t blocks_count);
	Formatter(BlockCache &cache, size_t blocks_count, size_t inodes_count);

	uint32_t magic() const;
	uint32_t block_size() const;
	uint32_t blocks_count() const;
	uint32_t inodes_count() const;

	uint32_t root_inode() const;
	void set_root_inode(uint32_t inode);

	Inode mkdir(uint32_t entries);
	Inode mkfile(uint32_t length);
	void free(Inode const &inode);

	uint32_t write(Inode &inode, uint8_t const *data, uint32_t len);
	void add_child(Inode &inode, char const *name, Inode const &child);

private:
	static uint32_t const FS_MAGIC;

	void format();
	Inode alloc_inode();
	uint32_t alloc_blocks(size_t count);

	BlockCache *cache_;

	BlockCache::BlockPtr super_page_;
	BlockCache::BlockPtr block_page_;
	BlockCache::BlockPtr inode_page_;

	uint32_t magic_;
	uint32_t blocks_count_;
	uint32_t inodes_count_;
	uint32_t root_inode_;
};

#endif /*__FORMAT_HPP__*/
