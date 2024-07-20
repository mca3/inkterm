#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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

void
draw(int fb)
{
	// Write out contents of screen
	for (int y = 0; y < term.rows; ++y) {
		for (int x = 0; x < term.cols; ++x) {
			fbc.row = y;
			fbc.col = x;

			fbc.is_inverted = (x == term.col && y == term.row);

			char c[] = {term.cells[(y*term.cols)+x].c, 0};
			if (c[0] && c[0] != ' ') fbink_print(fb, c, &fbc);
			else if (fbc.is_inverted) fbink_print(fb, " ", &fbc);
			// fbink_grid_refresh(fb, 1, 1, &fbc);
		}
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
		handle_pty_child(slave, "/bin/sh", args);
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

	int fb = fbink_open();
	if (!fb)
		die("fbink_open failed: %s\n", strerror(errno));

	int r;
	FBInkState s;
	r = fbink_init(fb, &fbc);
	fbink_cls(fb, &fbc, NULL, 0);
	fbink_get_state(&fbc, &s);

	assert(init_vt100(s.max_rows, s.max_cols, args) != -1);

	struct libevdev *dev = NULL;
	int fd, rc;
	fd = open("/dev/input/event2", O_RDONLY|O_NONBLOCK); // Very temporary
	if (rc = libevdev_new_from_fd(fd, &dev) == -1)
		die("failed to init libevdev: %s\n", strerror(-rc));

	struct pollfd pfds[] = {
		{ .fd = term.pty, .events = POLLIN },
		{ .fd = fd, .events = POLLIN },
	};

	static char buf[512] = {0};
	while ((rc = poll(pfds, sizeof(pfds)/sizeof(*pfds), 1000) != -1)) {
		if (pfds[0].revents & POLLIN) {
			int n;
			if ((n = read(term.pty, buf, sizeof(buf))) == -1)
				die("read: %s\n", strerror(errno));

			int o = vt100_write(buf, n);
			memmove(buf, buf+o, sizeof(buf)-o);
			draw(fb);
		}
		if (pfds[1].revents & POLLIN) {
			if (evdev_handle(dev)) draw(fb);
		}
	}
	die("poll: %s\n", strerror(errno));

	fbink_close(fb);
	vt100_free();
}
