#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
#include <ctype.h>

#include "term.h"
#include "x.h"

struct term term = {0};

void
draw(void)
{
	// Write out contents of screen
	for (int y = 0; y < term.rows; ++y) {
		for (int x = 0; x < term.cols; ++x) {
			unsigned char *c = utf8_encode(term.cells[(y*term.cols)+x].c, NULL);
			if (term.cells[(y*term.cols)+x].attr == ATTR_WIDEDUMMY) continue;
			else if (term.cells[(y*term.cols)+x].c) write(STDOUT_FILENO, c, strlen((char*)c)); // valid!
			else write(STDOUT_FILENO, " ", 1);
		}
		write(STDOUT_FILENO, "\n", 1);
	}
}

int
main(int argc, char *argv[])
{
	if (argc != 1) {
		fprintf(stderr, "usage: %s < input > output\n", argc ? argv[0] : "./test");
		exit(EXIT_FAILURE);
	}

	int slave;
	assert(term_init(&term, 10, 20, &slave) != -1);
	// close(slave); // We don't actually need it

	// Shuffle data between stdin and the PTY
	// This assumes everything just works (it might)
	int n, o = 0;
	static unsigned char buf[512] = {0};
	while ((n = read(STDIN_FILENO, buf+o, sizeof(buf)-o)) > 0) {
		o = term_write(&term, buf, n+o);

		/*
		// Make sure write succeeds
		n = 0;
		do n = write(STDOUT_FILENO, buf+n, o-n);
		while (n != -1 && o-n != 0);
		*/

		memmove(buf, buf+o, sizeof(buf)-o);
		o = sizeof(buf)-o;
	}

	// Intentionally wait to draw until here
	draw();

	term_free(&term);
}
