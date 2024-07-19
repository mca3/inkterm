#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "vt100.h"

int
main(int argc, char *argv[])
{
	char *args[] = {"/bin/sh", "-c", "echo \"Hello, world!\"; echo \"$$\"; sleep 1", NULL};
	vt100_init(4, 4, "/bin/sh", args);

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
				char c = term.cells[(y*term.cols)+x].c;
				if (c) putchar(c);
				else putchar(' ');
			}
			putchar('|');
			putchar('\n');
		}
		puts("$-$");
		printf("%d %d\n", term.row, term.col);
	}

	vt100_free();
}
