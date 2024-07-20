/* A lot of the code in here was inspired from Suckless's st.
 * Great terminal, not so great to read but still extremely helpful in guiding
 * how I wrote a lot of the code in here.
 *
 * Of course, this isn't a direct port of st to the framebuffer and there's a
 * lot that we don't support because I don't really want to go past the VT100
 * in terms of terminal emulation.
 * The VT100 is black and white only and most applications support it, so for
 * eInk which is also black and white only (mostly) it feels like a good thing
 * to target.
 *
 * I would assume we will eventually grow past the VT100 in some ways, but I
 * don't know if I would have it in me to do that.
 */

#define _POSIX_C_SOURCE 200112L

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#ifdef __linux__
#include <pty.h>
#else
#include <util.h>
#endif

#include "vt100.h"
#include "x.h"

struct term term = {0};

int
vt100_init(int rows, int cols, int *slave)
{
	int old_errno;

	// Sanity checks
	assert(rows > 0);
	assert(cols > 0);
	assert(slave);

	// Initialize the term struct.
	memset(&term, 0, sizeof(term));

	// Set fields
	term.rows = rows;
	term.cols = cols;

	// Set up the cells array.
	term.cells = malloc(sizeof(*term.cells)*rows*cols);
	if (!term.cells)
		goto fail;
	memset(term.cells, 0, sizeof(*term.cells)*rows*cols); 

	// Now that the term struct is initialized, we can set up a pty.
	if (openpty(&term.pty, slave, NULL, NULL, NULL) == -1)
		goto fail;

	// Everything was successful.
	return 0;

fail:
	// Everything below only matters if term is not NULL.
	old_errno = errno; /* free can set errno */

	// The pty is closed if we get here, because it is the last step that
	// can fail.

	if (term.cells) free(term.cells);

	// Restore errno
	errno = old_errno;

	return -1;
}

void
vt100_free(void)
{
	if (term.pty)
		// Is this a good idea?
		close(term.pty);

	if (term.cells)
		// term.cells==NULL is never true in normal usage, but it never
		// hurts to check.
		free(term.cells);
}

static void
newline(void)
{
	// Move everything back a line.
	memmove(term.cells, term.cells+sizeof(*term.cells)*term.cols, sizeof(*term.cells)*(term.rows-1)*term.cols);
	
	// Clear the line.
	memset(term.cells+(term.rows-1)*term.cols, 0, sizeof(*term.cells)*term.cols);
}

/* Handles control characters. */
static void
control(char c)
{
	switch (c) {
	case '\a': // BEL; Bell
		// TODO: Implement the bell
		break;
	case '\t': // TAB
		// Add 8 chars and round down to nearest 8.
		term.col = (term.col + 8) & ~0x3;
		if (term.col >= term.cols-1) {
			// Limit to the last column.
			term.col = term.cols-2;
		}
		break;
	case '\b': // BS; Backspace
		term.col = (term.col - 1) % term.cols;
		break;
	case '\r': // CR; Carriage return
		term.col = 0;
		break;
	case '\f': // FF; Form feed
	case '\v': // VT; Vertical tabulation
		// TODO. st does not implement these.
	case '\n':
		if (term.row == term.rows-1)
			newline();
		else
			term.row++;
		term.col = 0;
		break;
	default: /* do nothing */ break;
	}
}

void
tputc(char c)
{
	// Place the char and increment the cursor.
	term.cells[(term.cols*term.row)+term.col].c = c;

	if (term.col++ >= term.cols-1) {
		// Move to a new line.
		if (term.row == term.rows-1)
			newline();
		else
			term.row++;
		term.col = 0;
	}
}

size_t
vt100_write(char *buf, size_t n)
{
	if (n == 0)
		// No-op
		return 0;

	// Make sure buf isn't NULL.
	assert(buf);

	// Loop through all chars.
	char c;
	for (int i = 0, c = buf[0]; i < n; ++i,c=buf[i]) {
		if (iscntrl(c)) {
			control(c);
			continue;
		}

		tputc(c);
	}

	return n;
}
