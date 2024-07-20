CC = cc
CFLAGS = -O0 -std=c99 -pedantic -g -IFBInk `pkg-config --cflags libevdev`
LDFLAGS = -L./FBInk/Release -lfbink `pkg-config --libs libevdev`

OBJ = vt100.o evdev.o
PROG = main.o test.o

ifdef GCOV
	CFLAGS+=-fprofile-arcs -ftest-coverage
endif

%.o: %.c
	$(CC) -c -o $@ $(CFLAGS) $<

all: inkterm

inkterm: FBInk/Release/libfbink.so main.o $(OBJ)
	$(CC) -o $@ $(CFLAGS) main.o $(OBJ) $(LDFLAGS) 

test: test.o $(OBJ)
	$(CC) -o $@ $(CFLAGS) test.o $(OBJ) $(LDFLAGS) 

FBInk/Release/libfbink.so:
	$(MAKE) -C FBInk LINUX=1 MINIMAL=1 BITMAP=1 DRAW=1 FONTS=1 shared 

.PHONY: clean check

check: test
	@for i in testdata/*.in; do \
		printf '%s\n' "$$i"; \
		./test < "$$i" | cmp - "$${i%.in}.out"; \
	done

clean:
	rm -f $(OBJ)
	rm -f $(OBJ:.o=.gcno) $(OBJ:.o=.gcda)
	rm -f $(PROG)
	rm -f $(PROG:.o=.gcno) $(PROG:.o=.gcda)
	rm -f inkterm test
	#$(MAKE) -C FBInk clean
