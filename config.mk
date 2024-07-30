CC = cc
CFLAGS = -O0 -std=c99 -pedantic -Wall -Werror -g
LDFLAGS = 

LIBEVDEV_CFLAGS = `pkg-config --cflags libevdev`
LIBEVDEV_LDFLAGS = `pkg-config --libs libevdev`
FBINK_CFLAGS = -IFBInk 
FBINK_LDFLAGS = -L./FBInk/Release -lfbink

ifdef GCOV
	CFLAGS+=-fprofile-arcs -ftest-coverage
endif

ifdef NDEBUG
	CFLAGS+=-DNDEBUG
endif
