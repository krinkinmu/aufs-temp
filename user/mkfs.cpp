#include <iostream>
#include <iterator>
#include <fstream>
#include <cstring>
#include <string>
#include <vector>

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#include "format.hpp"

Inode copy_file(Formatter &format, std::string const &path)
{
	std::vector<char> data;

	{
		std::ifstream file(path.c_str(), std::ios::binary);
		if (!file)
			throw std::runtime_error("cannot open file");
		std::istream_iterator<char> begin(file), end;
		std::copy(begin, end, std::back_inserter(data));
	}

	Inode file_inode = format.mkfile(data.size());

	size_t written = 0;
	while (written != data.size())
	{
		written += format.write(file_inode,
				reinterpret_cast<uint8_t const *>(data.data()) + written,
				data.size() - written);
	}

	return file_inode;
}

Inode copy_dir(Formatter &format, std::string const &path)
{
	std::vector<std::string> entries;

	{
		struct dirent entry, *entryp = &entry;
		std::unique_ptr<DIR, int(*)(DIR *)> dirp(opendir(path.c_str()), &closedir);
		if (!dirp.get())
			throw std::runtime_error("cannot open dir");

		while (entryp)
		{
			readdir_r(dirp.get(), &entry, &entryp);
			if ( entryp && strcmp(entry.d_name, ".") &&
					strcmp(entry.d_name, "..") )
				entries.push_back(std::string(entry.d_name));
		}
	}

	Inode dir_inode = format.mkdir(entries.size());

	for (std::string const &entry : entries)
	{
		struct stat buffer;
		if (!stat((path + "/" + entry).c_str(), &buffer))
		{
			if (buffer.st_mode & S_IFDIR)
				format.add_child(dir_inode, entry.c_str(),
						copy_dir(format, path + "/" + entry));
			else
				format.add_child(dir_inode, entry.c_str(),
						copy_file(format, path + "/" + entry));
		}
	}

	return dir_inode;
}

int main(int argc, char **argv)
{
	if (argc < 2)
	{
		std::cout << "image file name expected" << std::endl;
		return 1;
	}

	if (argc > 3)
	{
		std::cout << "too many arguments" << std::endl;
		return 1;
	}

	try
	{
		BlockCache cache(argv[1], 4096);
		Formatter format(cache);

		if (argc == 3)
			format.set_root_inode(copy_dir(format, argv[2]).inode());
		else
			format.set_root_inode(format.mkdir(1).inode());
	}
	catch (std::exception const &ex)
	{
		std::cout << ex.what() << std::endl;
		return 1;
	}

	return 0;
}
