/* A lot of the code in here was inspired from Suckless's st.
 * Great terminal, not so great to read but still extremely helpful in guiding
 * how I wrote a lot of the code in here.
 *
 * Originally I wasn't going to go past the VT100, but I've come to realize
 * that the VT100 doesn't exactly have everything that many terminal
 * applications use today.
 */

#define _POSIX_C_SOURCE 200112L
#define _XOPEN_SOURCE

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

#ifdef __linux__
#include <pty.h>
#else
#include <util.h>
#endif

#include "term.h"
#include "utf8.h"
#include "x.h"

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
// When an attribute is applied, the value here is XOR'd onto term->attr the
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

// NOTE: Because inkterm targets e-readers, this is in light mode!
// TODO: Colors. fbink can do colors and will dither them.
const uint32_t colors[] = {
	0x00FFFFFF,
	0x00EEEEEE,
	0x00DDDDDD,
	0x00CCCCCC,
	0x00BBBBBB,
	0x00AAAAAA,
	0x00999999,
	0x00888888,
	0x00777777,
	0x00666666,
	0x00555555,
	0x00444444,
	0x00333333,
	0x00222222,
	0x00111111,
	0x00000000,
};

static uint32_t default_bg = colors[ 0],
		default_fg = colors[15];

/** Marks a cell at row/col as damaged. */
static inline void
damage(struct term *term, int row, int col)
{
	assert(row >= 0 && row <= term->rows-1);
	assert(col >= 0 && col <= term->cols-1);

	// Check to see if anything has even changed
	int idx = (row*term->cols)+col;
	if (memcmp(&term->cells[idx], &term->cells2[idx], sizeof(*term->cells)) == 0)
		// Nothing changed.
		return;

	int bit = idx % DAMAGE_WIDTH;
	int byt = idx / DAMAGE_WIDTH;
	term->damage[byt] |= (term_damage_t)(1)<<(bit);
}

/** Mark a single line as damaged. */
static inline void
damageline(struct term *term, int row)
{
	assert(row >= 0 && row <= term->rows-1);

	// Check to see if anything has even changed
	int idx = row*term->cols;
	if (memcmp(&term->cells[idx], &term->cells2[idx], sizeof(*term->cells)*term->cols) == 0)
		// Nothing changed.
		return;

	for (int i = 0; i < term->cols; ++i)
		damage(term, row, i);
}

/** Mark the entire screen as damaged. */
static inline void
damagescr(struct term *term)
{
	for (int i = 0; i < term->rows; ++i)
		damageline(term, i);
}

static inline void
init_row(struct term *term, int y)
{
	assert(y >= 0 && y < term->rows);

	memset(&term->cells[y*term->cols], 0, sizeof(*term->cells)*term->cols);

	// TODO: Do better!
	for (int i = 0; i < term->cols; i++) {
		term->cells[y*term->cols+i].bg = term->bg;
		term->cells[y*term->cols+i].fg = term->fg;
		term->cells[y*term->cols+i].attr = term->attr;
	}
}

static inline void
init_cells(struct term *term)
{
	// TODO: Do better!
	for (int i = 0; i < term->rows; i++) {
		init_row(term, i);
	}
}

static void
dellines(struct term *term, int row, int count)
{
	if (count == 0)
		return;
	else if (row+count >= term->margin_bottom)
		count = term->margin_bottom - row - 1;

	struct cell *dst = &term->cells[row*term->cols];
	struct cell *src = &term->cells[(row+count)*term->cols];

	memmove(dst, src, sizeof(*term->cells)*(term->margin_bottom-row-count+1)*term->cols);
	for (int i = term->margin_bottom-count+1; i <= term->margin_bottom; ++i)
		init_row(term, i);

	// TODO: This is lazy
	damagescr(term);
}

static void
newline(struct term *term, int firstcol)
{
	if (term->row < term->margin_bottom) {
		// There's still space on the screen, just move down one.
		// Move to the first column if requested.
		term_move(term, term->row+1, firstcol ? 0 : term->col);

		return;
	}

	// No space left on the screen.
	assert(term->row <= term->rows-1); // Sanity check

	// Move everything back a line, taking into account the margins.
	// Assertions are to make sure the margins are in line with what they should be.
	assert(term->margin_top >= 0 && term->margin_top < term->margin_bottom);
	assert(term->margin_bottom > term->margin_top && term->margin_bottom <= term->rows-1);

	memmove(
		&term->cells[term->margin_top*term->cols],
		&term->cells[(term->margin_top+1)*term->cols],
		sizeof(*term->cells)*((term->margin_bottom-term->margin_top)*term->cols)
	);

	// Clear the new line.
	memset(&term->cells[term->margin_bottom*term->cols], 0, sizeof(*term->cells)*term->cols);
	init_row(term, term->rows-1);

	// Damage everything in between the margins.
	// If the margins are 0 and term->rows-1, then damage the screen.
	if (term->margin_top == 0 && term->margin_bottom == term->rows-1)
		damagescr(term);
	else for (int row = term->margin_top; row < term->margin_bottom; ++row)
		damageline(term, row);

	// Move to the first column if requested.
	term_move(term, term->row, firstcol ? 0 : term->col);
}

/* Handles control characters. */
static void
control(struct term *term, rune c)
{
	switch (c) {
	case '\a': // BEL; Bell
		// Do something, if we are told to
		if (term->on_bell) term->on_bell();
		break;
	case '\t': // TAB
		// Add 8 chars and round down to nearest 8.
		// WRAPNEXT is automatically unset.
		term_move(term, term->row, (term->col + 8) & ~0x7);
		break;
	case '\b': // BS; Backspace
		term_move(term, term->row, term->col-1);
		break;
	case '\r': // CR; Carriage return
		term_move(term, term->row, 0);
		break;
	case '\f': // FF; Form feed
		// This is ^L which I use quite often.
		term_clear(term, 2);
		term_move(term, 0, 0);
		break;
	case '\v': // VT; Vertical tabulation
		// ???
	case '\n':
		newline(term, 0);
		break;
	default: /* do nothing */ break;
	}
}

static void
esc(struct term *term, rune c)
{
	switch (c) {
	case '7': // DECSC; DEC Save Cursor
		term->oldrow = term->row;
		term->oldcol = term->col;
		break;
	case '8': // DECRC; DEC Restore Cursor
		term_move(term, term->oldrow, term->oldcol);
		break;
	case '(':
		/* This asks the terminal to choose a different character set.
		 * Since that's kinda hard for us to do, we're just gonna
		 * ignore it for now :)
		 * But we do need to eat the next character that comes in. */

		// TODO: Replace with a less terrible workaround.
		term->state |= STATE_EATNEXT;
		break;
	default:
		printf("unknown esc: %c (0x%X)\n", c, c);
		break;
	}
}

static void
apply(struct term *term, int val)
{
	if (val < sizeof(attr_table)/sizeof(*attr_table)) {
		term->attr ^= attr_table[val];
		return;
	}

	// Colors!
	if (30 <= val && val <= 37) {
		term->fg = colors[val-30];
	} else if (40 <= val && val <= 47) {
		term->bg = colors[val-40];
	} else if (90 <= val && val <= 97) {
		term->fg = colors[val-90+8];
	} else if (100 <= val && val <= 107) {
		term->bg = colors[val-100+8];
	}

	// Everything else should be silently ignored.
}

static void
csi(struct term *term)
{
	int args[16] = {0};
	int narg = 0;
	// int priv = 0;

	// Parse the CSI code.
	char *buf = term->esc_buf;

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
		term->esc_state = ESC_NONE;
		return;
	}

	// Do stuff.
	// Codes are in no particular order.
	switch (*buf) {
	case 'A': // CUU; Cursor Up
		// Implicit 1 if no args given
		if (!narg) args[0] = 1;
		if (term->row > 0) term_move(term, term->row-args[0], term->col);
		break;
	case 'B': // CUU; Cursor Down
		// Implicit 1 if no args given
		if (!narg) args[0] = 1;
		if (term->row < term->rows-1) term_move(term, term->row+args[0], term->col);
		break;
	case 'C': // CUU; Cursor Forward
		// Implicit 1 if no args given
		if (!narg) args[0] = 1;
		if (term->col < term->cols-1) term_move(term, term->row, term->col+args[0]);
		break;
	case 'D': // CUU; Cursor Back
		// Implicit 1 if no args given
		if (!narg) args[0] = 1;
		if (term->col > 0) term_move(term, term->row, term->col-args[0]);
		break;
	case 'd': // VPA; Line Position Absolute
		if (!narg) args[0] = 1;
		term_move(term, args[0]-1, term->col);
		break;
	case 'G': // CHA: Cursor Character Absolute
		if (!narg) args[0] = 1;
		term_move(term, term->row, args[0]-1);
		break;
	case 'H': // CUP; Set cursor pos
	case 'f': // CUP; Set cursor pos
		if (!narg) args[0] = args[1] = 1; // Doubles as home
		term_move(term, args[0]-1, args[1]-1);
		break;
	case 'J': // Clear screen
		term_clear(term, args[0]);
		break;
	case 'K': // Clear line
		term_clearline(term, args[0]);
		break;
	case 'M': // DL; Delete Lines
		if (!narg) args[0]=1;
		dellines(term, term->row, args[0]);
		break;
	case 'm': // SGR; Set character attribute
		if (!narg) narg=1,args[0]=0;
		for (int i = 0; i < narg; i++) {
			if (args[i] == 0) {
				term->attr = 0;
				term->bg = default_bg;
				term->fg = default_fg;
			} else apply(term, args[i]);
		}
		break;
	case 'n': // DSR; Device status report
		if (args[0] == 6) {
			// Get cursor position
			int ret = snprintf(term->esc_buf, sizeof(term->esc_buf), "\033[%d;%dR", term->row+1, term->col+1);
			write(term->pty, term->esc_buf, ret);
		}
		break;
	case 'r': // DECSTBM; Set Top and Bottom Margins
		if (!args[0]) args[0] = 1;
		if (!args[1]) args[1] = term->rows;

		// Make sure the two arguments are actually sensible.
		if (args[0] >= args[1]) break;
		else if (args[0] <= 0 || args[0] > term->rows) break;
		else if (args[1] <= 0 || args[1] > term->rows) break;
		term->margin_top = args[0]-1;
		term->margin_bottom = args[1]-1;
		term_move(term, 0,0);
		break;
	case 'X': // ECH: Erase n Characters
		if (!args[0]) args[0] = 1;
		for (int i = term->col; i < term->cols && args[0]; i++,args[0]--) {
			term->cells[term->row*term->cols+i].c = 0;
			term->cells[term->row*term->cols+i].bg = term->bg;
			term->cells[term->row*term->cols+i].fg = term->fg;
			term->cells[term->row*term->cols+i].attr = term->attr;
			damage(term, term->row, i);
		}
		break;
	default:
		fprintf(stderr, "unknown CSI code %s (type = %c/0x%02x)\n", term->esc_buf, *buf, *buf);
		break;
	}

	term->esc_state = ESC_NONE;
}

static void
term_putr(struct term *term, rune c)
{
	// Check for control characters.
	if (c <= 0x1F) {
		if (c == '\033') {
			// Prepare for an escape code.
			term->esc_state = ESC_START;
			term->esc = 0;
			memset(term->esc_buf, 0, sizeof(term->esc_buf));
			return;
		}

		// Just a regular control character.
		control(term, c);
		return;
	} else if (term->esc_state == ESC_START) {
		if (c == '[') {
			term->esc_state = ESC_CSI;
			return;
		}
		esc(term, c);
		term->esc_state = ESC_NONE;
		return;
	} else if (term->esc_state == ESC_CSI) {
		// CSI code.
		term->esc_buf[term->esc++] = c&0xFF;
		if ((c >= 0x40 && c <= 0x7E) || term->esc+1 > sizeof(term->esc_buf)-1) {
			// ^ The final byte is in this range
			csi(term);
			term->esc_state = ESC_NONE;
		}

		return;
	}

	assert(term->row >= 0 && term->row <= term->rows-1);
	assert(term->col >= 0 && term->col <= term->cols-1);

	// If WRAPNEXT is set, wrap around to a new line.
	if (term->state & STATE_WRAPNEXT) {
		// This is the only time this is true.
		assert(term->col == term->cols-1);

		// Note: WRAPNEXT will be unset if needed by the call to
		// term_move.
		newline(term, 1);
	}

	// Place the char and increment the cursor.
	struct cell *cell = &term->cells[(term->cols*term->row)+term->col];
	cell->c = c;
	cell->attr = term->attr;
	cell->bg = term->bg;
	cell->fg = term->fg;
	damage(term, term->row, term->col);

	// Wide characters have a dummy cell placed ahead of it.
	if (wcwidth(c) == 2 && term->col+1 <= term->cols-1) {
		term->cells[(term->cols*term->row)+term->col+1].c = 0;
		term->cells[(term->cols*term->row)+term->col+1].attr = ATTR_WIDEDUMMY;
		damage(term, term->row, term->col+1);
	}

	// Move forward a column if it doesn't go off screen.
	// If it does, don't move forward and wrap on the next character.
	if (wcwidth(c) == 2 && term->col+1 < term->cols-1)
		term_move(term, term->row, term->col+2); // Wide chars move ahead two spots
	else if (term->col < term->cols-1)
		term_move(term, term->row, term->col+1);
	else
		term->state |= STATE_WRAPNEXT;
}

void
term_flip(struct term *term)
{
	memmove(term->cells2, term->cells, sizeof(*term->cells)*term->rows*term->cols);
}

int
term_init(struct term *term, int rows, int cols, int *slave)
{
	int old_errno;

	// Sanity checks
	assert(rows > 0);
	assert(cols > 0);
	assert(slave);

	// Initialize the term struct.
	memset(term, 0, sizeof(*term));

	// Set fields
	term->rows = rows;
	term->cols = cols;
	term->bg = default_bg;
	term->fg = default_fg;

	// The bottom margin is always the number of rows unless explicitly set otherwise.
	term->margin_bottom = rows-1;

	// Set up the cells array.
	term->cells = malloc(sizeof(*term->cells)*rows*cols);
	if (!term->cells)
		goto fail;
	memset(term->cells, 0, sizeof(*term->cells)*rows*cols);
	init_cells(term);

	// Set up the second cells array, for double buffering.
	term->cells2 = malloc(sizeof(*term->cells2)*rows*cols);
	if (!term->cells)
		goto fail;
	memmove(term->cells2, term->cells, sizeof(*term->cells2)*rows*cols);

	// Setup the damage array.
	term->damage = malloc(DAMAGE_BYTES(term));
	if (!term->damage)
		goto fail;
	memset(term->damage, 0, DAMAGE_BYTES(term));

	// Now that the term struct is initialized, we can set up a pty.
	if (openpty(&term->pty, slave, NULL, NULL, NULL) == -1)
		goto fail;

	// Set terminal size.
	// This isn't fatal.
	struct winsize w = {0};
	w.ws_row = rows;
	w.ws_col = cols;
	if (ioctl(term->pty, TIOCSWINSZ, &w) < 0)
		fprintf(stderr, "TIOCSWINSZ failed: %s\n", strerror(errno));

	// Everything was successful.
	return 0;

fail:
	// Everything below only matters if term is not NULL.
	old_errno = errno; /* free can set errno */

	// The pty is closed if we get here, because it is the last step that
	// can fail.

	if (term->damage) free(term->damage);
	if (term->cells) free(term->cells);
	if (term->cells2) free(term->cells2);

	// Restore errno
	errno = old_errno;

	return -1;
}

void
term_free(struct term *term)
{
	if (term->pty)
		// Is this a good idea?
		close(term->pty);

	if (term->damage)
		free(term->damage);

	if (term->cells)
		free(term->cells);

	if (term->cells2)
		free(term->cells2);
}

size_t
term_write(struct term *term, unsigned char *buf, size_t n)
{
	if (n == 0)
		// No-op
		return 0;

	// Make sure buf isn't NULL.
	assert(buf);

	// Loop through all chars.
	int i,j;
	for (i=j=0; i < n; i+=j) {
		if (term->state & STATE_EATNEXT) {
			// TODO: Use a less terrible workaround.
			// See esc.
			j = 1;
			term->state &= ~STATE_EATNEXT;
			continue;
		}

		rune r = 0;
		if ((j = utf8_decode(buf+i, n-i, &r)) == 0)
			return i;
		term_putr(term, r);
	}

	return n;
}

void
term_move(struct term *term, int y, int x)
{
	term->row = y;
	term->col = x;

	// Bounds check
	if (term->col < 0) term->col = 0;
	else if (term->col > term->cols-1) term->col = term->cols-1;
	if (term->row < 0) term->row = 0;
	else if (term->row > term->rows-1) term->row = term->rows-1;

	// Unset STATE_WRAPNEXT.
	// If STATE_WRAPNEXT is set, then the next character that is put on the
	// screen will wrap around; often this is not what we want.
	term->state &= ~STATE_WRAPNEXT;
}

void
term_clear(struct term *term, int dir)
{
	switch (dir) {
	case 0: // ED0; Clear screen from cursor down
		memset(
			&term->cells[(term->row*term->cols)+term->col],
			0,
			sizeof(*term->cells)*((term->rows*term->cols)-((term->row*term->cols)+term->col))
		);
		for (int i = 0; i < term->col; i++) {
			term->cells[(term->row*term->cols)+i].bg = term->bg;
			term->cells[(term->row*term->cols)+i].fg = term->fg;
			term->cells[(term->row*term->cols)+i].attr = term->attr;
		}
		for (int i = term->row+1; i < term->rows; ++i)
			init_row(term, i);
		break;
	case 1: // ED1; Clear screen from cursor up
		if (term->row >= 1)
			memset(term->cells, 0, sizeof(*term->cells)*(((term->row-1)*term->cols)+term->col));
		for (int i = 0; i < term->row; ++i)
			init_row(term, i);
		for (int i = 0; i <= term->col; i++) {
			term->cells[(term->row*term->cols)+i].c = 0;
			term->cells[(term->row*term->cols)+i].bg = term->bg;
			term->cells[(term->row*term->cols)+i].fg = term->fg;
			term->cells[(term->row*term->cols)+i].attr = term->attr;
		}
		break;
	case 2: // ED2; Clear screen
		memset(term->cells, 0, sizeof(*term->cells)*term->rows*term->cols);
		init_cells(term);
		break;
	}

	damagescr(term);
}

void
term_clearline(struct term *term, int dir)
{
	struct cell *row = &term->cells[(term->row*term->cols)];

	switch (dir) {
	case 0: // EL0; Clear line from cursor right
		memset(row+term->col, 0, sizeof(*row)*(term->cols-term->col));
		for (int i = term->col; i < term->cols; i++) {
			row[i].bg = term->bg;
			row[i].fg = term->fg;
			row[i].attr = term->attr;
			damage(term, term->row, i);
		}
		break;
	case 1: // EL1; Clear line from cursor left
		memset(row, 0, sizeof(*row)*(term->col));
		for (int i = 0; i < term->col; i++) {
			row[i].bg = term->bg;
			row[i].fg = term->fg;
			row[i].attr = term->attr;
			damage(term, term->row, i);
		}
		break;
	case 2: // EL2; Clear line
		memset(row, 0, sizeof(*row)*(term->cols));
		init_row(term, term->row);
		damageline(term, term->row);
		break;
	}
}
