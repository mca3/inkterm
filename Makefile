CC = cc
CFLAGS = -O0 -std=c99 -pedantic -g -IFBInk `pkg-config --cflags libevdev`
LDFLAGS = -L./FBInk/Release -lfbink `pkg-config --libs libevdev`

OBJ = main.o vt100.o

%.o: %.c
	$(CC) -c -o $@ $(CFLAGS) $<

all: inkterm

inkterm: FBInk/Release/libfbink.so $(OBJ)
	$(CC) -o $@ $(CFLAGS) $(OBJ) $(LDFLAGS) 

FBInk/Release/libfbink.so:
	$(MAKE) -C FBInk LINUX=1 MINIMAL=1 BITMAP=1 DRAW=1 FONTS=1 shared 

.PHONY: clean

clean:
	rm -f main.o vt100.o inkterm
	#$(MAKE) -C FBInk clean
