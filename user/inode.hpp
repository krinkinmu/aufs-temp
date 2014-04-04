#ifndef __INODE_HPP__
#define __INODE_HPP__

#include <cstdint>

#include "cache.hpp"

static uint32_t const FS_FILENAME_MAXLEN = 28;

struct inode
{
	uint32_t block;
	uint32_t blocks;
	uint32_t length;
	uint32_t uid;
	uint32_t gid;
	uint32_t mode;
	uint64_t ctime;
};

struct dir_entry
{
	char name[FS_FILENAME_MAXLEN];
	uint32_t inode;
};

class Inode
{
public:
	Inode();

	uint32_t inode() const;
	uint32_t block() const;
	uint32_t blocks() const;
	uint32_t length() const;
	uint64_t ctime() const;
	uint32_t uid() const;
	uint32_t gid() const;
	uint32_t mode() const;
	explicit operator bool() const;

	friend class Formatter;

private:
	Inode(BlockCache &cache, uint32_t ino);

	void set_length(uint32_t);
	void set_block(uint32_t);
	void set_blocks(uint32_t);
	void set_ctime(uint64_t);
	void set_uid(uint32_t);
	void set_gid(uint32_t);
	void set_mode(uint32_t);

	struct inode *data();
	struct inode const *data() const;

	uint32_t inode_;
	BlockCache::BlockPtr block_;
	size_t index_;
};

#endif /*__INODE_HPP__*/
