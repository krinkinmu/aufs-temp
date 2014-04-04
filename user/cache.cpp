#include <algorithm>
#include <stdexcept>
#include <tuple>

#include "cache.hpp"

BlockCache::BlockCache(std::string const &img, std::size_t block_size)
	: fd_(img.c_str())
	, block_size_(block_size)
	, blocks_count_(device_size()/block_size_)
{
	if (!fd_)
		throw std::runtime_error("image open error");
}

BlockCache::~BlockCache()
{ try { flush(); } catch (...) { } }

BlockCache::BlockPtr BlockCache::block(size_t no)
{
	std::map<size_t, BlockCache::BlockPtr>::iterator it;
	bool inserted;
	std::tie(it, inserted) = blocks_.emplace(no, std::make_shared<Block>(block_size(), no));
	if (inserted)
		parse_block(it->second);
	return it->second;
}

void BlockCache::flush()
{
	std::for_each(std::begin(blocks_), std::end(blocks_),
			[&](std::map<size_t, BlockPtr>::value_type const &p)
			{ drop_block(p.second); });

	std::map<size_t, BlockPtr>::const_iterator const begin(std::begin(blocks_));
	std::map<size_t, BlockPtr>::const_iterator const end(std::end(blocks_));
	for (std::map<size_t, BlockPtr>::const_iterator it(begin); it != end;)
	{
		if (it->second.unique())
			it = blocks_.erase(it);
		else
			++it;
	}
}

size_t BlockCache::block_size() const
{ return block_size_; }

size_t BlockCache::blocks_count() const
{ return blocks_count_; }

void BlockCache::drop_block(BlockPtr const &b)
{
	size_t const offset = block_no_to_offset(b->block_no());
	b->dump(fd_.seekp(offset));
}

void BlockCache::parse_block(BlockPtr &b)
{
	size_t const offset = block_no_to_offset(b->block_no());
	b->parse(fd_.seekg(offset));
}

size_t BlockCache::block_no_to_offset(size_t no) const
{ return no * block_size(); }

size_t BlockCache::device_size()
{
	fd_.seekg(0, std::ios_base::end);
	return static_cast<size_t>(fd_.tellg());
}
