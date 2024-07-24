#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <libevdev/libevdev.h>
#include <linux/input.h>

#include "vt100.h"
#include "x.h"
#include "evdev.h"

static int mod_state = 0;

#define MOD_SHIFT (1 << 0)
#define MOD_ALT   (1 << 1)
#define MOD_CTRL  (1 << 2)

/* This is very temporary code. */
static char
map_code(int code, int val)
{
	char c = 0;

	// Handle modifiers first.
	switch (code) {
	case KEY_LEFTSHIFT:
	case KEY_RIGHTSHIFT:
		if (val)
			mod_state |= MOD_SHIFT;
		else
			mod_state &= ~MOD_SHIFT;
		return 0;
	case KEY_LEFTCTRL:
	case KEY_RIGHTCTRL:
		if (val)
			mod_state |= MOD_CTRL;
		else
			mod_state &= ~MOD_CTRL;
		return 0;
	}

	if (val == 0)
		return 0;

	// Handle all "letters" now.
	switch (code) {
	case KEY_ESC:		return '\033'; break;
	case KEY_1:		c = '1'; break;
	case KEY_2:		c = '2'; break;
	case KEY_3:		c = '3'; break;
	case KEY_4:		c = '4'; break;
	case KEY_5:		c = '5'; break;
	case KEY_6:		c = '6'; break;
	case KEY_7:		c = '7'; break;
	case KEY_8:		c = '8'; break;
	case KEY_9:		c = '9'; break;
	case KEY_0:		c = '0'; break;
	case KEY_Q:		c = 'q'; break;
	case KEY_W:		c = 'w'; break;
	case KEY_E:		c = 'e'; break;
	case KEY_R:		c = 'r'; break;
	case KEY_T:		c = 't'; break;
	case KEY_Y:		c = 'y'; break;
	case KEY_U:		c = 'u'; break;
	case KEY_I:		c = 'i'; break;
	case KEY_O:		c = 'o'; break;
	case KEY_P:		c = 'p'; break;
	case KEY_ENTER:		c = '\n'; break;
	case KEY_A:		c = 'a'; break;
	case KEY_S:		c = 's'; break;
	case KEY_D:		c = 'd'; break;
	case KEY_F:		c = 'f'; break;
	case KEY_G:		c = 'g'; break;
	case KEY_H:		c = 'h'; break;
	case KEY_J:		c = 'j'; break;
	case KEY_K:		c = 'k'; break;
	case KEY_L:		c = 'l'; break;
	case KEY_Z:		c = 'z'; break;
	case KEY_X:		c = 'x'; break;
	case KEY_C:		c = 'c'; break;
	case KEY_V:		c = 'v'; break;
	case KEY_B:		c = 'b'; break;
	case KEY_N:		c = 'n'; break;
	case KEY_M:		c = 'm'; break;
	case KEY_BACKSPACE:	c = '\177'; break;
	case KEY_SPACE:		c = ' '; break;
	case KEY_SEMICOLON:	c = ';'; break;
	case KEY_MINUS:		c = '-'; break;
	case KEY_DOT:		c = '.'; break;
	case KEY_TAB:		c = '\t'; break;
	case KEY_BACKSLASH:	c = '\\'; break;
	case KEY_SLASH:		c = '/'; break;
	}

	if (mod_state & MOD_SHIFT)
		if (c == ';') return ':';
		else if (c >= '0' && c <= '9') return (")!@#$%^&*(")[c-'0'];
		else if (c == '-') return '_';
		else if (c == '=') return '+';
		else return toupper(c);
	else if (mod_state & MOD_CTRL)
		if (c >= 'A' && c <= 'Z') return c-'A';
	return c;
}

static char *
map_special(int code, int *outlen)
{
#define X(kc, out) case kc: *outlen = sizeof((out))-1;return (out);
	switch (code) {
	X(KEY_UP,	"\033[A")
	X(KEY_DOWN,	"\033[B")
	X(KEY_LEFT,	"\033[D")
	X(KEY_RIGHT,	"\033[C")
	}
	return NULL;
}

int
evdev_handle(struct libevdev *dev)
{
	int dirty = 0;
	int rc;

	assert(dev);

	do {
		struct input_event ev;
		rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
		if (rc == 0 && ev.type == EV_KEY) {
			printf("Event: %s %s %d\n",
			       libevdev_event_type_get_name(ev.type),
			       libevdev_event_code_get_name(ev.type, ev.code),
			       ev.value);
			int len;
			char *str;
			if (ev.value != 0 && (str = map_special(ev.code, &len))) {
				write(term.pty, str, len);
				dirty = 1;
			} else {
				char c = map_code(ev.code, ev.value);
				if (c) {
					write(term.pty, &c, 1);
					dirty = 1;
				}
			}
		}
	} while (rc == 1);

	return dirty;
}
