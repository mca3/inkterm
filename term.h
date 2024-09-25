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

/* Helper defines for damage tracking. */
#define DAMAGE_WIDTH (8*sizeof(term_damage_t))
#define DAMAGE_ROW(term, idx) ((idx)/((term)->cols))
#define DAMAGE_COL(term, idx) ((idx)%((term)->cols))
#define DAMAGE_BITS(term) ((term)->rows*(term)->cols)
#define DAMAGE_BYTES(term) ((DAMAGE_BITS(term)+(sizeof(term_damage_t)))/8)

#include "utf8.h"

// TODO: Investigate why unsigned long raises an assertion in xkbcommon
typedef unsigned short term_damage_t;

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
	struct cell *cells2; // double buffer!
	term_damage_t *damage;

	char attr;
	char state;

	char esc_buf[ESC_BUFSZ];
	int esc;
	int esc_state;

	void (*on_bell)(void);
};

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
int term_init(struct term *term, int rows, int cols, int *slave);

/** Frees all related data with the term struct. */
void term_free(struct term *term);

/* Copies the content of the current state of the screen to the off-screen
 * buffer for faster damage tracking.
 * Call this after rendering has taken place.
 */
void term_flip(struct term *term);

/** Write data to the terminal.
 * The return value is how many bytes that were read from the input.
 *
 * buf must not be null unless n is 0.
 */
size_t term_write(struct term *term, unsigned char *buf, size_t n);

/** Move the cursor in an absolute fashion. */
void term_move(struct term *term, int y, int x);

/** Clear the screen.
 *
 * 0 clears from cursor down.
 * 1 clears from cursor up.
 * 2 clears the entire screen.
 * Anything else does nothing.
 */
void term_clear(struct term *term, int dir);

/** Clear a line on the screen.
 *
 * 0 clears to the right of the cursor.
 * 1 clears to the left of the cursor.
 * 2 clears the entire line.
 * Anything else does nothing.
 */
void term_clearline(struct term *term, int dir);

#endif /* ifndef TERM_H */
