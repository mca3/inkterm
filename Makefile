include ./config.mk

ALL_CFLAGS = $(CFLAGS) $(LIBEVDEV_CFLAGS) $(FBINK_CFLAGS) -Ivt
ALL_LDFLAGS = $(LDFLAGS) $(LIBEVDEV_LDFLAGS) $(FBINK_LDFLAGS) -Lvt -lvt

OBJ = main.o evdev.o

%.o: %.c
	$(CC) -c -o $@ $(ALL_CFLAGS) $<

all: inkterm

inkterm: FBInk/Release/libfbink.so vt/libvt.a $(OBJ)
	$(CC) -o $@ $(ALL_CFLAGS) $^ $(ALL_LDFLAGS) 

FBInk/Release/libfbink.so:
	$(MAKE) -C FBInk LINUX=1 MINIMAL=1 BITMAP=1 DRAW=1 FONTS=1 shared 

vt/libvt.a:
	$(MAKE) -C vt libvt.a

.PHONY: clean check

check:
	$(MAKE) -C vt check

clean:
	rm -f $(OBJ)
	rm -f $(OBJ:.o=.gcno) $(OBJ:.o=.gcda)
	rm -f inkterm
	#$(MAKE) -C FBInk clean
	$(MAKE) -C vt clean
