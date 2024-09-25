CC = cc
CFLAGS = -O2 -std=c99 -pedantic -Wall -Werror -g -IFBInk -Ilibxkbcommon/include -Ilibevdev
LDFLAGS = -LFBInk/Release -lfbink -Llibxkbcommon/build -lxkbcommon -Llibevdev/build -levdev -static
DESTDIR = _install

OBJ = term.o evdev.o utf8.o
LIBS = FBInk/Release/libfbink.a libevdev/build/libevdev.a libxkbcommon/build/libxkbcommon.a
PROG = main.o test.o

ifdef GCOV
	CFLAGS+=-fprofile-arcs -ftest-coverage
endif

%.o: %.c
	$(CC) -c -o $@ $(CFLAGS) $<

all: inkterm

inkterm: main.o $(OBJ) $(LIBS)
	$(CC) -o $@ $(CFLAGS) main.o $(OBJ) $(LDFLAGS)

test: test.o $(OBJ) $(LIBS)
	$(CC) -o $@ $(CFLAGS) test.o $(OBJ) $(LDFLAGS)

#
# Libraries
#

FBInk/Release/libfbink.a:
	$(MAKE) -C FBInk LINUX=1 MINIMAL=1 BITMAP=1 DRAW=1 FONTS=1 static

libxkbcommon/build/build.ninja:
	cd libxkbcommon && meson setup build \
		--backend=ninja \
		-Denable-x11=false -Denable-wayland=false \
		-Ddefault_library=static

libxkbcommon/build/libxkbcommon.a: libxkbcommon/build/build.ninja
	cd libxkbcommon && meson compile -C build xkbcommon

libevdev/build/build.ninja:
	cd libevdev && meson setup build \
		--backend=ninja \
		-Dtests=disabled \
		-Ddocumentation=disabled \
		-Ddefault_library=static

libevdev/build/libevdev.a: libevdev/build/build.ninja
	cd libevdev && meson compile -C build evdev

#
# Usual phony targets.
#

.PHONY: clean clean-libs check install

install: inkterm
	mkdir -p $(DESTDIR)
	install -m755 inkterm $(DESTDIR)
	install -m644 LICENSE $(DESTDIR)/LICENSE.inkterm
	install -m644 libevdev/COPYING $(DESTDIR)/LICENSE.libevdev
	install -m644 libxkbcommon/LICENSE $(DESTDIR)/LICENSE.libxkbcommon
	install -m644 FBInk/LICENSE $(DESTDIR)/LICENSE.fbink

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

clean-libs:
	rm -rf libxkbcommon/build
	rm -rf libevdev/build
	$(MAKE) -C FBInk clean
