#ifndef __BLOCK_HPP__
#define __BLOCK_HPP__

#include <ostream>
#include <istream>
#include <cstdint>
#include <cstddef>
#include <vector>

class Block
{
public:
	explicit Block(size_t block_size, size_t block_no = 0)
		: block(block_no), data_(block_size, 0)
	{}

	Block(Block &&) = delete;
	Block(Block const &) = delete;
	Block &operator=(Block const &) = delete;
	Block &operator=(Block &&b) = delete;

	size_t block_no() const
	{ return block; }

	void set_block_no(size_t no)
	{ block = no; }

	size_t block_size() const
	{ return data_.size(); }

	uint8_t at(size_t byte) const
	{ return data_.at(byte); }

	uint8_t& at(size_t byte)
	{ return data_.at(byte); }

	std::vector<uint8_t>::iterator begin()
	{ return std::begin(data_); }

	std::vector<uint8_t>::iterator end()
	{ return std::end(data_); }

	uint8_t *data()
	{ return data_.data(); }

	uint8_t const *data() const
	{ return data_.data(); }

	std::ostream& dump(std::ostream &out) const
	{ return out.write(reinterpret_cast<std::ostream::char_type const *>(data()), block_size()); }

	std::istream& parse(std::istream &in)
	{ return in.read(reinterpret_cast<char *>(data()), block_size()); }

private:
	size_t block;
	std::vector<uint8_t> data_;
};

#endif /*__BLOCK_HPP__*/
