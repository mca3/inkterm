#ifndef VT100_H
#define VT100_H

#define ESC_BUFSZ 64

struct cell {
	char c;
};

struct term {
	int rows, cols;
	int row, col;
	int pty;
	struct cell *cells;

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
void vt100_putc(char c);

/** Write data to the terminal.
 * The return value is how many bytes that were read from the input.
 *
 * buf must not be null unless n is 0.
 */
size_t vt100_write(char *buf, size_t n);

/** Move the cursor in a relative fashion.
 * This will scroll as needed. */
void vt100_moverel(int x, int y);

/** Clear the screen. */
void vt100_clear(void);

#endif /* ifndef VT100_H */
