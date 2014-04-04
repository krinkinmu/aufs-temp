CXX=g++
CFLAGS=-Wall -Wextra -Werror -std=c++11 -pedantic -g

mkfs.aufs: mkfs.o cache.o inode.o format.o
	$(CXX) $(CFLAGS) mkfs.o cache.o inode.o format.o -o mkfs.aufs

cache.o: cache.cpp cache.hpp block.hpp
	$(CXX) $(CFLAGS) -c cache.cpp -o cache.o

inode.o: inode.cpp inode.hpp
	$(CXX) $(CFLAGS) -c inode.cpp -o inode.o

format.o: format.cpp format.hpp
	$(CXX) $(CFLAGS) -c format.cpp -o format.o

mkfs.o: mkfs.cpp cache.hpp
	$(CXX) $(CFLAGS) -c mkfs.cpp -o mkfs.o

clean:
	rm -rf *.o mkfs.aufs

.PHONY: clean
