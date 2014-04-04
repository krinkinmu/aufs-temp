#ifndef __BLOCK_CACHE_HPP__
#define __BLOCK_CACHE_HPP__

#include <fstream>
#include <memory>
#include <map>

#include "block.hpp"

class BlockCache
{
public:
	typedef std::shared_ptr<Block> BlockPtr;

	BlockCache(std::string const &img, std::size_t block_size);
	~BlockCache();

	BlockCache(BlockCache const &) = delete;
	BlockCache &operator=(BlockCache const &) = delete;

	BlockCache(BlockCache &&bc) noexcept = delete;
	BlockCache &operator=(BlockCache &&bc) noexcept = delete;

	BlockPtr block(size_t no);
	void flush();
	size_t block_size() const;
	size_t blocks_count() const;

private:
	std::fstream fd_;
	size_t block_size_;
	size_t blocks_count_;
	std::map<size_t, BlockPtr> blocks_;

	void drop_block(BlockPtr const &b);
	void parse_block(BlockPtr &b);
	size_t block_no_to_offset(size_t no) const;
	size_t device_size();
};

#endif /*__BLOCK_CACHE_HPP__*/
