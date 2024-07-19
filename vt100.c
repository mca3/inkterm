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
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>

#ifdef __linux__
#include <pty.h>
#else
#include <util.h>
#endif

#include "vt100.h"
#include "x.h"

struct term term = {0};
int child_pid;

static void
sigchld_handler(int _)
{
	int reason;
	pid_t p;

	// Attempt to wait for our child.
	// WNOHANG is used here to, as you may have guessed, not hang us.
	if ((p = waitpid(child_pid, &reason, WNOHANG)) == -1)
		die("failed to wait for child %d: %s\n", child_pid, strerror(errno));
	else if (p == 0)
		// Nothing to do
		return;

	// Something happened, and there's a chance it could be fatal.
	if (WIFSIGNALED(reason))
		// TODO: I would like to have human readable signal names here.
		// Everyone knows 9, but there are more.
		die("child received signal %d\n", WTERMSIG(reason));
	else if (WEXITSTATUS(reason) != 0)
		die("child exited with status code %d\n", WEXITSTATUS(reason));

	// Child exited peacefully.
	exit(0);
}

/* Sets up the child for the psudeoterminal. */
static void
handle_pty_child(int pty, char *path, char *argv[])
{

	// setsid gives our child process its own group, so when we
	// kill the parent nothing happens to it.
	setsid();

	// Setup FDs.
	dup2(pty, STDIN_FILENO);
	dup2(pty, STDOUT_FILENO);
	dup2(pty, STDERR_FILENO);

	// Set the master FD as our controlling terminal
	if (ioctl(pty, TIOCSCTTY, NULL) == -1)
		die("failed to set controlling terminal: %s\n", strerror(errno));

	// So long as the slave FD isn't stdin, stdout, or stderr, close it to
	// free up a FD. We already have three references to it.
	switch (pty) {
	case STDIN_FILENO:
	case STDOUT_FILENO:
	case STDERR_FILENO:
		break;
	default:
		close(pty);
		break;
	}

	// Get user info.
	struct passwd *pw;
	errno = 0;
	if ((pw = getpwuid(getuid())) == NULL)
		die("getpwuid: %s\n", strerror(errno));

	// Setup environment.
	unsetenv("COLUMNS");
	unsetenv("LINES");
	unsetenv("TURNCAP");
	setenv("LOGNAME", pw->pw_name, 1);
	setenv("USER", pw->pw_name, 1);
	setenv("SHELL", "/bin/sh", 1); // TODO
	setenv("HOME", pw->pw_dir, 1);
	setenv("TERM", "vt100", 1); // Intentionally hardwired
	
	// Set the signals and away we go!
	signal(SIGCHLD, SIG_DFL);
	signal(SIGHUP, SIG_DFL);
	signal(SIGINT, SIG_DFL);
	signal(SIGQUIT, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
	signal(SIGALRM, SIG_DFL);

	execvp(path, argv);
	exit(EXIT_FAILURE); /* unreachable, most of the time */
}

int
vt100_init(int rows, int cols, char *path, char *argv[])
{
	int old_errno;

	// Sanity checks
	assert(rows > 0);
	assert(cols > 0);
	assert(path);
	assert(argv);

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
	int slave;
	if (openpty(&term.pty, &slave, NULL, NULL, NULL) == -1)
		goto fail;

	// Fork and start the process.
	switch ((child_pid = fork())) {
	case -1: goto fail; break;
	case 0:	/* child */
		close(term.pty);
		handle_pty_child(slave, path, argv);
		abort(); /* unreachable */
		break;
	default:/* parent */
		// Close the slave, setup signals, and return.
		close(slave);
		signal(SIGCHLD, sigchld_handler);
		break;
	}

	// Everything was successful.
	return 0;

fail:
	// Everything below only matters if term is not NULL.
	old_errno = errno; /* free can set errno */

	if (child_pid == -1) {
		// This is the only time where it is okay to close both
		// fds.
		close(term.pty);
		close(slave);
	}
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
		term.col += 8;
		term.col %= term.cols;
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

static void
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
