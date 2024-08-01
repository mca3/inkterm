#ifndef VT100_H
#define VT100_H

#define ESC_BUFSZ 64

//#define ATTR_BOLD		(1 << 0)
//#define ATTR_LOW		(1 << 1)
//#define ATTR_UNDERLINE	(1 << 2)
//#define ATTR_BLINK		(1 << 3)
#define ATTR_REVERSE		(1 << 4)
//#define ATTR_INVIS		(1 << 5)

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
};

extern struct term term;

/** Initializes the terminal with the number of rows and cols.
 * All data is overwritten in the passed struct.
 *
 * vt100_init does not concern itself with who is on the other side of the PTY
 * and how it is set up.
 *
 * slave must be non NULL or an assertion is raised.
 * rows and cols must be non-zero and positive or an assertion will be raised.
 *
 * If memory was unable to be allocated, -1 is returned and errno is set.
 */
int vt100_init(int rows, int cols, int *slave);

/** Frees all related data with the term struct. */
void vt100_free(void);

/** Writes the specified character to the terminal. */
void vt100_putr(rune c);

/** Write data to the terminal.
 * The return value is how many bytes that were read from the input.
 *
 * buf must not be null unless n is 0.
 */
size_t vt100_write(unsigned char *buf, size_t n);

/** Move the cursor in an absolute fashion. */
void vt100_move(int x, int y);

/** Move the cursor in a relative fashion. */
void vt100_moverel(int x, int y);

/** Clear the screen.
 * 
 * 0 clears from cursor down.
 * 1 clears from cursor up.
 * 2 clears the entire screen.
 * Anything else does nothing.
 */
void vt100_clear(int dir);

/** Clear a line on the screen.
 * 
 * 0 clears to the right of the cursor.
 * 1 clears to the left of the cursor.
 * 2 clears the entire line.
 * Anything else does nothing.
 */
void vt100_clearline(int dir);

#endif /* ifndef VT100_H */
