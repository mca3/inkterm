#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <fbink.h>

#include "vt100.h"
#include "x.h"

int
main(int argc, char *argv[])
{
	char *args[] = {"/bin/sh", "-c", "echo \"Hello, world!\"; echo \"$$\"; sleep 1", NULL};

	int fb = fbink_open();
	if (!fb)
		die("fbink_open failed: %s\n", strerror(errno));

	int r;
	FBInkConfig fbc = {
		.is_verbose = 1,
		.fontname = SCIENTIFICA,
	};
	FBInkState s;
	r = fbink_init(fb, &fbc);
	fbink_cls(fb, &fbc, NULL, 0);
	fbink_get_state(&fbc, &s);

	vt100_init(s.max_rows, s.max_cols, "/bin/sh", args);

	// TODO: The rest of the owl.
	// For now, just shuffle between the pty and stdout
	static char buf[512] = {0};
	int n;
	while ((n = read(term.pty, buf, sizeof(buf))) != -1) {
		int o = vt100_write(buf, n);
		printf("took %d\n", o);
		memmove(buf, buf+o, sizeof(buf)-o);

		// Write out contents of screen
		for (int y = 0; y < term.rows; ++y) {
			for (int x = 0; x < term.cols; ++x) {
				fbc.row = y;
				fbc.col = x;

				fbc.is_inverted = (x == term.col && y == term.row);

				char c[] = {term.cells[(y*term.cols)+x].c, 0};
				if (c[0] && c[0] != ' ') fbink_print(fb, c, &fbc);
				else if (fbc.is_inverted) fbink_print(fb, " ", &fbc);
			}
		}
		printf("%d %d\n", term.row, term.col);
	}

	fbink_close(fb);
	vt100_free();
}
