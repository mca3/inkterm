#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
#include <ctype.h>

#include <fbink.h>
#include <libevdev/libevdev.h>
#include <linux/input.h>

#include "vt100.h"
#include "x.h"

static FBInkConfig fbc = {
	//.is_verbose = 1,
	.fontname = SCIENTIFICA,
};

void
draw(int fb)
{
	// Write out contents of screen
	for (int y = 0; y < term.rows; ++y) {
		for (int x = 0; x < term.cols; ++x) {
			fbc.row = y;
			fbc.col = x;

			fbc.is_inverted = (x == term.col && y == term.row);

			char c[] = {term.cells[(y*term.cols)+x].c, 0};
			if (c[0] && c[0] != ' ') fbink_print(fb, c, &fbc);
			else if (fbc.is_inverted) fbink_print(fb, " ", &fbc);
			// fbink_grid_refresh(fb, 1, 1, &fbc);
		}
	}
}

char
map_code(int code, int val)
{
	static int shift = 0;
	char c = 0;

	switch (code) {
	case KEY_LEFTSHIFT:
		shift = !!val;
		break;
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
	case KEY_BACKSPACE:	c = '\b'; break;
	case KEY_SPACE:		c = ' '; break;
	}

	if (shift)
		return toupper(c);
	return c;
}

void
handleev(int fb, struct libevdev *dev)
{
	int dirty = 0;
	int rc;
	do {
		struct input_event ev;
		rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
		if (rc == 0 && ev.type == EV_KEY) {
			printf("Event: %s %s %d\n",
			       libevdev_event_type_get_name(ev.type),
			       libevdev_event_code_get_name(ev.type, ev.code),
			       ev.value);
			char c = map_code(ev.code, ev.value);
			write(term.pty, &c, 1);
			putchar(c);
			//tputc(c);
			dirty = 1;
		}
	} while (rc == 1);

	if (dirty) draw(fb);
}

int
main(int argc, char *argv[])
{
	char *args[] = {"/bin/sh", NULL};//"-c", "echo \"Hello, world!\"; echo \"$$\"; sleep 1", NULL};

	int fb = fbink_open();
	if (!fb)
		die("fbink_open failed: %s\n", strerror(errno));

	int r;
	FBInkState s;
	r = fbink_init(fb, &fbc);
	fbink_cls(fb, &fbc, NULL, 0);
	fbink_get_state(&fbc, &s);

	vt100_init(s.max_rows, s.max_cols, "/bin/sh", args);

	struct libevdev *dev = NULL;
	int fd, rc;
	fd = open("/dev/input/event1", O_RDONLY|O_NONBLOCK); // Very temporary
	if (rc = libevdev_new_from_fd(fd, &dev) == -1)
		die("failed to init libevdev: %s\n", strerror(-rc));

	struct pollfd pfds[] = {
		{ .fd = term.pty, .events = POLLIN },
		{ .fd = fd, .events = POLLIN },
	};

	static char buf[512] = {0};
	while ((rc = poll(pfds, sizeof(pfds)/sizeof(*pfds), 1000) != -1)) {
		if (pfds[0].revents & POLLIN) {
			int n;
			if ((n = read(term.pty, buf, sizeof(buf))) == -1)
				die("read: %s\n", strerror(errno));

			int o = vt100_write(buf, n);
			memmove(buf, buf+o, sizeof(buf)-o);
			draw(fb);
		}
		if (pfds[1].revents & POLLIN) {
			handleev(fb, dev);
		}
	}
	die("poll: %s\n", strerror(errno));

	fbink_close(fb);
	vt100_free();
}
