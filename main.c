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
#include "evdev.h"
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
	fd = open("/dev/input/event2", O_RDONLY|O_NONBLOCK); // Very temporary
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
			if (evdev_handle(dev)) draw(fb);
		}
	}
	die("poll: %s\n", strerror(errno));

	fbink_close(fb);
	vt100_free();
}
