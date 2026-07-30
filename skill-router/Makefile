# skillrouter - consolidated skill-library interface/router.
# Stdlib + statically-linked SQLite amalgamation only. No package-manager
# dependencies: third_party/sqlite3.{h,c} is vendored.

CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -I.
CC       ?= gcc
SQLITE_DEFS = -DSQLITE_THREADSAFE=1 -DSQLITE_ENABLE_FTS5

MINGW_CXX ?= x86_64-w64-mingw32-g++
MINGW_CC  ?= x86_64-w64-mingw32-gcc

.PHONY: all capi test clean windows

all: skillrouter libskillrouter.a

sqlite3.o: third_party/sqlite3.c
	$(CC) -O2 -c third_party/sqlite3.c -o sqlite3.o $(SQLITE_DEFS)

sqlite3_pic.o: third_party/sqlite3.c
	$(CC) -O2 -fPIC -c third_party/sqlite3.c -o sqlite3_pic.o $(SQLITE_DEFS)

skillrouter: main.cpp skill_library.hpp mcp_server.hpp sqlite3.o
	$(CXX) $(CXXFLAGS) main.cpp sqlite3.o -o $@ -lpthread -ldl

test_library: test_library.cpp skill_library.hpp mcp_server.hpp sqlite3.o
	$(CXX) $(CXXFLAGS) test_library.cpp sqlite3.o -o $@ -lpthread -ldl

skilllib_c.o: skilllib_c.cpp skilllib_c.h skill_library.hpp
	$(CXX) $(CXXFLAGS) -c skilllib_c.cpp -o skilllib_c.o

skilllib_c_pic.o: skilllib_c.cpp skilllib_c.h skill_library.hpp
	$(CXX) $(CXXFLAGS) -fPIC -DSKILLLIB_BUILD_SHARED -c skilllib_c.cpp -o skilllib_c_pic.o

libskillrouter.a: skilllib_c.o sqlite3.o
	ar rcs $@ skilllib_c.o sqlite3.o

libskillrouter.so: skilllib_c_pic.o sqlite3_pic.o
	$(CXX) -shared skilllib_c_pic.o sqlite3_pic.o -o $@ -lpthread -ldl

test_c_api: test_c_api.cpp skilllib_c.cpp skilllib_c.h skill_library.hpp sqlite3.o
	$(CXX) $(CXXFLAGS) test_c_api.cpp skilllib_c.cpp sqlite3.o -o $@ -lpthread -ldl

capi: libskillrouter.a libskillrouter.so test_c_api

test: test_library test_c_api
	./test_library
	./test_c_api

sqlite3_win.o: third_party/sqlite3.c
	$(MINGW_CC) -O2 -c third_party/sqlite3.c -o sqlite3_win.o $(SQLITE_DEFS)

windows: skillrouter.exe

skillrouter.exe: main.cpp skill_library.hpp mcp_server.hpp sqlite3_win.o
	$(MINGW_CXX) -std=c++20 -O2 -static -static-libgcc -static-libstdc++ \
	    -I. main.cpp sqlite3_win.o -o $@ -lws2_32

clean:
	rm -f skillrouter skillrouter.exe test_library test_c_api \
	    libskillrouter.a libskillrouter.so \
	    sqlite3.o sqlite3_pic.o sqlite3_win.o skilllib_c.o skilllib_c_pic.o \
	    *.db *.db-wal *.db-shm
