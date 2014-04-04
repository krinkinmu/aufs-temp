#include <ctime>

#include <arpa/inet.h>
#include <sys/types.h>
#include <unistd.h>

#include "inode.hpp"

namespace {

	uint64_t htonll(uint64_t n)
	{
		uint64_t test = 1ull;
		if (*(char *)&test == 1ull)
			return (static_cast<uint64_t>(htonl(n & 0xffffffff)) << 32u) |
				static_cast<uint64_t>(htonl(n >> 32u));
		else
			return n;
	}

	uint64_t ntohll(uint64_t n)
	{ return htonll(n); }

}

uint32_t Inode::inode() const
{ return inode_; }

uint32_t Inode::block() const
{ return ntohl(data()->block); }

void Inode::set_block(uint32_t block)
{ data()->block = htonl(block); }

uint32_t Inode::blocks() const
{ return ntohl(data()->blocks); }

void Inode::set_blocks(uint32_t blocks)
{ data()->blocks = htonl(blocks); }

uint32_t Inode::length() const
{ return ntohl(data()->length); }

void Inode::set_length(uint32_t length)
{ data()->length = htonl(length); }

uint64_t Inode::ctime() const
{ return htonll(data()->ctime); }

void Inode::set_ctime(uint64_t t)
{ data()->ctime = htonll(t); }

uint32_t Inode::uid() const
{ return ntohl(data()->uid); }

void Inode::set_uid(uint32_t id)
{ data()->uid = htonl(id); }

uint32_t Inode::gid() const
{ return ntohl(data()->gid); }

void Inode::set_gid(uint32_t id)
{ data()->gid = htonl(id); }

uint32_t Inode::mode() const
{ return ntohl(data()->mode); }

void Inode::set_mode(uint32_t mode)
{ data()->mode = htonl(mode); }

Inode::Inode(BlockCache &cache, uint32_t ino)
	: inode_(ino)
	, block_(ino ? cache.block(3 + ino / (cache.block_size() / sizeof(struct inode))) : nullptr)
	, index_(ino % (cache.block_size() / sizeof(struct inode)))
{
	if (*this)
	{
		set_block(0);
		set_blocks(0);
		set_length(0);
		set_ctime(time(NULL));
		set_uid(getuid());
		set_gid(getgid());
		set_mode(493);
	}
}

Inode::Inode()
	: inode_(0)
	, block_(nullptr)
	, index_(0)
{ }

struct inode *Inode::data()
{ return reinterpret_cast<struct inode *>(block_->data()) + index_; }

struct inode const *Inode::data() const
{ return reinterpret_cast<struct inode *>(block_->data()) + index_; }

Inode::operator bool() const
{ return inode_; }
