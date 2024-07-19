#ifndef VT100_H
#define VT100_H

struct cell {
	char c;
};

struct term {
	int rows, cols;
	int row, col;
	int pty;
	struct cell *cells;
};

extern struct term term;
extern int child_pid;

/** Initializes the terminal with the number of rows and cols.
 * All data is overwritten in the passed struct.
 *
 * A child process is created and calls execv(path, argv).
 * Please make sure path exists and argv has a NULL terminator.
 *
 * t must be non-null, or an assertion will be raised.
 * rows and cols must be non-zero and positive or an assertion will be raised.
 * path and argv must not be NULL or an assertion is raised.
 *
 * If memory was unable to be allocated, -1 is returned and errno is set.
 */
int vt100_init(int rows, int cols, char *path, char *argv[]);

/** Frees all related data with the term struct. */
void vt100_free(void);

/** Write data to the terminal.
 * The return value is how many bytes that were read from the input.
 *
 * buf must not be null unless n is 0.
 */
size_t vt100_write(char *buf, size_t n);

#endif /* ifndef VT100_H */
