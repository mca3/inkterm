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

#include "utf8.h"
#include "vt100.h"
#include "x.h"

struct term term = {0};

/* for esc_state */
enum {
	ESC_NONE,
	ESC_START,
	ESC_CSI
};

// This table is for the \033[X;Ym type sequences.
// We only support single digit ones.
// And in fact of those we support two: reset and reverse.
//
// When an attribute is applied, the value here is XOR'd onto term.attr the
// attribute being enabled is 0 (reset).
static int attr_table[] = {
	[0] =	0,		// Reset
	[1] =	0,		// Bold
	[2] =	0,		// Low intensity
	[3] =	0,		// N/A
	[4] =	0,		// Underline
	[5] =	0,		// Blinking
	[6] =	0,		// N/A
	[7] =	ATTR_REVERSE,	// Reverse
	[8] =	0,		// Invisible
};

/** Marks a cell at row/col as damaged. */
static inline void
damage(int row, int col)
{
	assert(row >= 0 && row <= term.rows-1);
	assert(col >= 0 && col <= term.cols-1);

	int idx = (row*term.cols)+col;
	int bit = idx%8;
	int byt = idx/8;
	term.damage[byt] |= 1<<(bit);
}

/** Mark a single line as damage. */
static inline void
damageline(int row)
{
	// This function is sloppy because I don't care enough to make it not
	// sloppy.

	for (int i = 0; i < term.cols; ++i)
		damage(row, i);
}

/** Mark the entire screen as damaged. */
static inline void
damagescr(void)
{
	int sz = (term.rows*term.cols)/8;
	memset(term.damage, 0xFF, sz);
}

static void
newline(int firstcol)
{
	if (term.row < term.margin_bottom) {
		// There's still space on the screen, just move down one.
		// Move to the first column if requested.
		vt100_move(firstcol ? 0 : term.col, term.row+1);

		return;
	}

	// No space left on the screen.
	assert(term.row <= term.rows-1); // Sanity check

	// Move everything back a line, taking into account the margins.
	// Assertions are to make sure the margins are in line with what they should be.
	assert(term.margin_top >= 0 && term.margin_top < term.margin_bottom);
	assert(term.margin_bottom > term.margin_top && term.margin_bottom <= term.rows-1);
	memmove(
		&term.cells[term.margin_top*term.cols],
		&term.cells[(term.margin_top+1)*term.cols],
		sizeof(*term.cells)*((term.margin_bottom-term.margin_top)*term.cols)
	);
	
	// Clear the new line.
	memset(&term.cells[term.margin_bottom*term.cols], 0, sizeof(*term.cells)*term.cols);

	// Damage everything in between the margins.
	// If the margins are 0 and term.rows-1, then damage the screen.
	if (term.margin_top == 0 && term.margin_bottom == term.rows-1)
		damagescr();
	else for (int row = term.margin_top; row < term.margin_bottom; ++row)
		damageline(row);

	// Move to the first column if requested.
	vt100_move(firstcol ? 0 : term.col, term.row);
}

/* Handles control characters. */
static void
control(rune c)
{
	switch (c) {
	case '\a': // BEL; Bell
		// TODO: Implement the bell
		break;
	case '\t': // TAB
		// Add 8 chars and round down to nearest 8.
		// WRAPNEXT is automatically unset.
		vt100_move((term.col + 8) & ~0x7, term.row);
		break;
	case '\b': // BS; Backspace
		vt100_moverel(-1, 0);
		break;
	case '\r': // CR; Carriage return
		vt100_move(0, term.row);
		break;
	case '\f': // FF; Form feed
		// This is ^L which I use quite often.	
		vt100_clear(2);
		vt100_move(0, 0);
		break;
	case '\v': // VT; Vertical tabulation
		// ???
	case '\n':
		newline(0);
		break;
	default: /* do nothing */ break;
	}
}

static void
esc(rune c)
{
	switch (c) {
	case '7': // DECSC; DEC Save Cursor
		term.oldrow = term.row;
		term.oldcol = term.col;
		break;
	case '8': // DECRC; DEC Restore Cursor
		vt100_move(term.oldcol, term.oldrow);
		break;
	default: /* do nothing */ break;
	}
}

static void
csi(void)
{
	int args[16] = {0};
	int narg = 0;
	// int priv = 0;

	// Parse the CSI code.
	char *buf = term.esc_buf;

	// The first char might be a private mode indicator.
	if (*buf >= 0x3C && *buf <= 0x3F) { // "<=>?"
		// Private mode.
		// TODO: Handle private final byte.
		// priv = 1;
		buf++;
	}

	for (; *buf; buf++) {
		if (*buf >= 0x40 && *buf <= 0x7F) {
			// Final byte.
			break;
		}

		// Parse the number and add it on.
		if (*buf >= '0' && *buf <= '9') {
			if (!narg)
				narg=1;

			args[narg-1] *= 10;
			args[narg-1] += *buf - '0';
		} else if (*buf == ';' || *buf == ':') {
			// Next argument.
			// st allows ; or :, but it only allows one of them.
			if (++narg == sizeof(args)/sizeof(*args))
				// Too many args
				break;
		}
	}

	if (*buf < 0x40 && *buf > 0x7F) {
		// This is not a final byte, so the escape code was probably truncated.
		term.esc_state = ESC_NONE;
		return;
	}

	// Do stuff.
	// Codes are in no particular order.
	switch (*buf) {
	case 'A': // CUU; Cursor Up
		// Implicit 1 if no args given
		if (!narg) args[0] = 1;
		if (term.row > 0) vt100_moverel(0, -args[0]);
		break;
	case 'B': // CUU; Cursor Down
		// Implicit 1 if no args given
		if (!narg) args[0] = 1;
		if (term.row < term.rows-1) vt100_moverel(0, args[0]);
		break;
	case 'C': // CUU; Cursor Forward
		// Implicit 1 if no args given
		if (!narg) args[0] = 1;
		if (term.col < term.cols-1) vt100_moverel(args[0], 0);
		break;
	case 'D': // CUU; Cursor Back
		// Implicit 1 if no args given
		if (!narg) args[0] = 1;
		if (term.col > 0) vt100_moverel(-args[0], 0);
		break;
	case 'H': // CUP; Set cursor pos
	case 'f': // CUP; Set cursor pos
		if (!narg) args[0] = args[1] = 1; // Doubles as home
		vt100_move(args[1]-1, args[0]-1);
		break;
	case 'J': // Clear screen
		vt100_clear(args[0]);
		break;
	case 'K': // Clear line
		vt100_clearline(args[0]);
		break;
	case 'm': // SGR; Set character attribute
		if (!narg) narg=1,args[0]=1;
		for (int i = 0; i < narg; i++) {
			args[i]--; // One indexed

			if (args[i] == 0) term.attr = 0;
			else if (args[i] < sizeof(attr_table)/sizeof(*attr_table)) term.attr ^= attr_table[args[i]];
		}
		break;
	case 'n': // DSR; Device status report
		if (args[0] == 6) {
			// Get cursor position 
			int ret = snprintf(term.esc_buf, sizeof(term.esc_buf), "\033[%d;%dR", term.row+1, term.col+1);
			write(term.pty, term.esc_buf, ret);
		}
		break;
	case 'r': // DECSTBM; Set Top and Bottom Margins
		if (!args[0]) args[0] = 1;
		if (!args[1]) args[1] = term.rows;

		// Make sure the two arguments are actually sensible.
		if (args[0] >= args[1]) break;
		else if (args[0] <= 0 || args[0] > term.rows) break;
		else if (args[1] <= 0 || args[1] > term.rows) break;
		term.margin_top = args[0]-1;
		term.margin_bottom = args[1]-1;
		vt100_move(0,0);
		break;
	default:
		fprintf(stderr, "unknown CSI code %s (type = %c/0x%02x)\n", term.esc_buf, *buf, *buf);
		break;
	}

	term.esc_state = ESC_NONE;
}

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

	// The bottom margin is always the number of rows unless explicitly set otherwise.
	term.margin_bottom = rows-1;

	// Set up the cells array.
	term.cells = malloc(sizeof(*term.cells)*rows*cols);
	if (!term.cells)
		goto fail;
	memset(term.cells, 0, sizeof(*term.cells)*rows*cols); 

	// Setup the damage array.
	term.damage = malloc(sizeof(*term.damage)*(rows*cols+1));
	if (!term.damage)
		goto fail;
	memset(term.damage, 0, (rows*cols+1)/sizeof(*term.damage)); 

	// Now that the term struct is initialized, we can set up a pty.
	if (openpty(&term.pty, slave, NULL, NULL, NULL) == -1)
		goto fail;

	// Set terminal size.
	// This isn't fatal.
	struct winsize w = {0};
	w.ws_row = rows;
	w.ws_col = cols; // TODO: I broke something here!
	if (ioctl(term.pty, TIOCSWINSZ, &w) < 0)
		fprintf(stderr, "TIOCSWINSZ failed: %s\n", strerror(errno));

	// Everything was successful.
	return 0;

fail:
	// Everything below only matters if term is not NULL.
	old_errno = errno; /* free can set errno */

	// The pty is closed if we get here, because it is the last step that
	// can fail.

	if (term.damage) free(term.damage);
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

	if (term.damage)
		// term.damage==NULL is never true in normal usage, but it
		// never hurts to check.
		free(term.damage);

	if (term.cells)
		// term.cells==NULL is never true in normal usage, but it never
		// hurts to check.
		free(term.cells);
}

void
vt100_putr(rune c)
{
	// Check for control characters.
	if (c <= 0x1F) {
		if (c == '\033') {
			// Prepare for an escape code.
			term.esc_state = ESC_START;
			term.esc = 0;
			memset(term.esc_buf, 0, sizeof(term.esc_buf));
			return;
		}

		// Just a regular control character.
		control(c);
		return;
	} else if (term.esc_state == ESC_START) {
		if (c == '[') {
			term.esc_state = ESC_CSI;
			return;
		}
		esc(c);
		term.esc_state = ESC_NONE;
		return;
	} else if (term.esc_state == ESC_CSI) {
		// CSI code.
		term.esc_buf[term.esc++] = c&0xFF;
		if ((c >= 0x40 && c <= 0x7E) || term.esc+1 > sizeof(term.esc_buf)-1) {
			// ^ The final byte is in this range
			csi();
			term.esc_state = ESC_NONE;
		}

		return;
	}

	assert(term.row >= 0 && term.row <= term.rows-1);
	assert(term.col >= 0 && term.col <= term.cols-1);

	// If WRAPNEXT is set, wrap around to a new line.
	if (term.state & STATE_WRAPNEXT) {
		// This is the only time this is true.
		assert(term.col == term.cols-1);

		// Note: WRAPNEXT will be unset if needed by the call to
		// vt100_moverel.
		newline(1);
	}

	// Place the char and increment the cursor.
	term.cells[(term.cols*term.row)+term.col].c = c;
	term.cells[(term.cols*term.row)+term.col].attr = term.attr;
	damage(term.row, term.col);

	// Move forward a column if it doesn't go off screen.
	// If it does, don't move forward and wrap on the next character.
	if (term.col < term.cols-1)
		vt100_moverel(1, 0);
	else
		term.state |= STATE_WRAPNEXT;
}

size_t
vt100_write(unsigned char *buf, size_t n)
{
	if (n == 0)
		// No-op
		return 0;

	// Make sure buf isn't NULL.
	assert(buf);

	// Loop through all chars.
	int i,j;
	for (i=j=0; i < n; i+=j) {
		rune r = 0;
		if ((j = utf8_decode(buf+i, n-i, &r)) == 0)
			return i;
		vt100_putr(r);
	}

	return n;
}

void
vt100_move(int x, int y)
{
	term.row = y;
	term.col = x;

	// Bounds check
	if (term.col < 0) term.col = 0;
	else if (term.col > term.cols-1) term.col = term.cols-1;
	if (term.row < 0) term.row = 0;
	else if (term.row > term.rows-1) term.row = term.rows-1;

	// Unset STATE_WRAPNEXT.
	// If STATE_WRAPNEXT is set, then the next character that is put on the
	// screen will wrap around; often this is not what we want.
	term.state &= ~STATE_WRAPNEXT;
}

void
vt100_moverel(int x, int y)
{
	// This function exists so I don't have to rewrite code.
	// TODO: Rewrite code, remove this function.
	vt100_move(term.col+x, term.row+y);
}

void
vt100_clear(int dir)
{
	damagescr();

	switch (dir) {
	case 0: // ED0; Clear screen from cursor down
		memset(&term.cells[(term.row*term.cols)+term.col], 0, sizeof(*term.cells)*((term.rows*term.cols)-((term.row*term.cols)+term.col)));
		break;
	case 1: // ED1; Clear screen from cursor up
		memset(term.cells, 0, sizeof(*term.cells)*((term.row*term.cols)+term.col));
		break;
	case 2: // ED2; Clear screen
		memset(term.cells, 0, sizeof(*term.cells)*term.rows*term.cols);
		break;
	}
}

void
vt100_clearline(int dir)
{
	struct cell *row = &term.cells[(term.row*term.cols)];

	damageline(term.row);

	switch (dir) {
	case 0: // EL0; Clear line from cursor right
		memset(row+term.col, 0, sizeof(*row)*(term.cols-term.col));
		break;
	case 1: // EL1; Clear line from cursor left
		memset(row, 0, sizeof(*row)*(term.col));
		break;
	case 2: // EL2; Clear line
		memset(row, 0, sizeof(*row)*(term.cols));
		break;
	}
}
