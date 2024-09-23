#ifndef TERM_H
#define TERM_H

#define ESC_BUFSZ 64

//#define ATTR_BOLD		(1 << 0)
//#define ATTR_LOW		(1 << 1)
//#define ATTR_UNDERLINE	(1 << 2)
//#define ATTR_BLINK		(1 << 3)
#define ATTR_REVERSE		(1 << 4)
//#define ATTR_INVIS		(1 << 5)
#define ATTR_WIDEDUMMY		(1 << 6)

/** Used to control if the terminal will wrap to another line on the next
 * character. */
#define STATE_WRAPNEXT		(1 << 0)

#include "utf8.h"

struct cell {
	rune c;
	char attr;
};

struct term {
	int rows, cols;
	int row, col;
	int oldrow, oldcol;

	/** These are for DECSTBM. */
	int margin_top, margin_bottom;

	int pty;
	struct cell *cells;
	unsigned char *damage;

	char attr;
	char state;

	char esc_buf[ESC_BUFSZ];
	int esc;
	int esc_state;

	void (*on_bell)(void);
};

extern struct term term;

/** Initializes the terminal with the number of rows and cols.
 * All data is overwritten in the passed struct.
 *
 * term_init does not concern itself with who is on the other side of the PTY
 * and how it is set up.
 *
 * slave must be non NULL or an assertion is raised.
 * rows and cols must be non-zero and positive or an assertion will be raised.
 *
 * If memory was unable to be allocated, -1 is returned and errno is set.
 */
int term_init(int rows, int cols, int *slave);

/** Frees all related data with the term struct. */
void term_free(void);

/** Writes the specified character to the terminal. */
void term_putr(rune c);

/** Write data to the terminal.
 * The return value is how many bytes that were read from the input.
 *
 * buf must not be null unless n is 0.
 */
size_t term_write(unsigned char *buf, size_t n);

/** Move the cursor in an absolute fashion. */
void term_move(int y, int x);

/** Clear the screen.
 * 
 * 0 clears from cursor down.
 * 1 clears from cursor up.
 * 2 clears the entire screen.
 * Anything else does nothing.
 */
void term_clear(int dir);

/** Clear a line on the screen.
 * 
 * 0 clears to the right of the cursor.
 * 1 clears to the left of the cursor.
 * 2 clears the entire line.
 * Anything else does nothing.
 */
void term_clearline(int dir);

#endif /* ifndef TERM_H */
