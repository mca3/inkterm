#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <fbink.h>
#include <libevdev/libevdev.h>
#include <linux/input.h>

#include "vt100.h"
#include "evdev.h"
#include "x.h"

static FBInkConfig fbc = {
	//.is_verbose = 1,
	.fontname = SCIENTIFICA,
};

static int child_pid;

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

	/*
	// Something happened, and there's a chance it could be fatal.
	if (WIFSIGNALED(reason))
		// TODO: I would like to have human readable signal names here.
		// Everyone knows 9, but there are more.
		die("child received signal %d\n", WTERMSIG(reason));
	else if (WEXITSTATUS(reason) != 0)
		die("child exited with status code %d\n", WEXITSTATUS(reason));

	// Child exited peacefully.
	exit(0);
	*/
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
	//setenv("SHELL", "/bin/sh", 1); // TODO
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

static inline void
draw_cell(int fb, int y, int x)
{
	if (y > term.rows-1 || x > term.cols-1) return;
	fbc.row = y;
	fbc.col = x;

	fbc.is_inverted = !!(term.cells[(y*term.cols)+x].attr & ATTR_REVERSE);
	fbc.is_inverted ^= (x == term.col && y == term.row);

	char c[] = {term.cells[(y*term.cols)+x].c, 0};
	if (c[0]) fbink_print(fb, c, &fbc);
	else fbink_print(fb, " ", &fbc);
	fbink_grid_refresh(fb, 1, 1, &fbc);
}

void
draw(int fb)
{
	// Write out contents of screen
	static int last_row = 0;
	static int last_col = 0;

	// Redraw the cell that the cursor was on
	if (last_row != term.row || last_col != term.col)
		draw_cell(fb, last_row, last_col);
	last_row = term.row;
	last_col = term.col;

	// This function is kinda dense because we're doing damage tracking.
	// Essentially this means that whenever a cell changes, we mark it as
	// "damaged", and we need to repaint it.
	// The loop below here goes through all damage entries (unsigned chars)
	// and checks to see if there is any damage, and if there is, then we
	// will repaint the affected cells.
	int r, c;
	for (int byt = 0; byt < (term.rows*term.cols)/8; ++byt) {
		if (!term.damage[byt])
			// No damage branch. Keep on going.
			continue;

		// Damage in this region.
		int idx = byt*8;
		unsigned char p = term.damage[byt];
		for (int bit = 0; bit < 8; ++bit) {
			// Each bit here represents a cell.
			if (!!(p&(1<<bit))) {
				// This cell is damaged.
				r = (idx+bit)/term.cols;
				c = (idx+bit)%term.cols;
				draw_cell(fb, r, c);
			}
		}

		// Unmark the damage.
		term.damage[byt] = 0;
	}
}

int
init_vt100(int rows, int cols, char *args[])
{
	int slave;
	if (vt100_init(rows, cols, &slave) == -1)
		die("failed to init vt100: %s\n", strerror(errno));

	// Fork and start the process.
	switch ((child_pid = fork())) {
	case -1: goto fail; break;
	case 0:	/* child */
		close(term.pty);
		handle_pty_child(slave, args[0], args);
		abort(); /* unreachable */
		break;
	default:/* parent */
		// Close the slave, setup signals, and return.
		close(slave);
		signal(SIGCHLD, sigchld_handler);
		break;
	}

	return 0;

fail:
	if (child_pid == -1) {
		// This is the only time where it is okay to close both
		// fds.
		close(term.pty);
		close(slave);
	}

	vt100_free();
	return -1;
}

int
main(int argc, char *argv[])
{
	char *args[] = {"/bin/sh", NULL};//"-c", "echo \"Hello, world!\"; echo \"$$\"; sleep 1", NULL};
	char *shell = getenv("SHELL");
	if (shell) {
		args[0] = shell;
	}

	int fb = fbink_open();
	if (!fb)
		die("fbink_open failed: %s\n", strerror(errno));

	FBInkState s;
	if (fbink_init(fb, &fbc) != EXIT_SUCCESS) {
		// Header says that this should only happen on reMarkable devices.
		die("failed to initialize fbink\n");
	}

	fbink_cls(fb, &fbc, NULL, 0);
	fbink_get_state(&fbc, &s);

	if (init_vt100(s.max_rows, s.max_cols, args) != 0)
		die("failed to init vt: %s\n", strerror(errno));

	struct libevdev *dev = NULL;
	int fd, rc;
	fd = open("/dev/input/event2", O_RDONLY|O_NONBLOCK); // Very temporary
	if (flock(fd, LOCK_EX) == -1)
		die("failed to get exclusive lock for evdev: %s\n", strerror(errno));
	rc = 1;
	if (ioctl(fd, EVIOCGRAB, &rc) == -1)
		die("failed to grab event deivce: %s\n", strerror(errno));
	
	if ((rc = libevdev_new_from_fd(fd, &dev)) == -1)
		die("failed to init libevdev: %s\n", strerror(-rc));

	struct pollfd pfds[] = {
		{ .fd = term.pty, .events = POLLIN },
		{ .fd = fd, .events = POLLIN },
	};

	static char buf[512] = {0};
	int n, o = 0;
	while ((rc = poll(pfds, sizeof(pfds)/sizeof(*pfds), 1000) != -1)) {
		if (pfds[1].revents & POLLIN)
			evdev_handle(dev);

		if (pfds[0].revents & POLLIN) {
			if ((n = read(term.pty, buf+o, sizeof(buf)-o)) == -1)
				die("read: %s\n", strerror(errno));

			// TODO: This will be a problem if I ever do UTF-8
			//o = vt100_write(buf, n+o);
			vt100_write(buf, n);
			//memmove(buf, buf+o, sizeof(buf)-o);
			//o = sizeof(buf)-o;
		}

		draw(fb);
	}
	die("poll: %s\n", strerror(errno));

	fbink_close(fb);
	vt100_free();
}
