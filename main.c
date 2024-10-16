#if 1
#define _POSIX_C_SOURCE 199309L
#include <time.h>
#endif

#include <errno.h>
#include <getopt.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/wait.h>

#include <fbink.h>
#include <libevdev/libevdev.h>
#include <xkbcommon/xkbcommon.h>

#include "evdev.h"
#include "term.h"
#include "x.h"

static FBInkConfig fbc = {
	//.is_verbose = 1,
	.fontname = SCIENTIFICA,
};

static int child_pid;

/** Turning this knob up or down will increase throughput at the cost of screen
 * update latency. */
static int draw_timeout = 10;

/* Internal variable that refreshes the screen on the next call to draw.
 * Most of the time, this is immediately after it is set. */
static int refresh_next = 0;

/* Some keys require special handling. */
static struct {
	int key;
	struct {
		int sz;
		const char *data;
	} val;
} string_binds[] = {
	// Purely to avoid calling strlen at runtime, the val struct is used to
	// give a size to a string. The KEYSTR macro fills val for us.
	// sizeof(str) includes NUL; we don't when we write to the pty
#define KEYSTR(str) { .sz = (sizeof((str))-1), .data = (str) }
	// Kernel keycodes, not XKB! Though, this may change sooner or later.
	{ KEY_UP,		KEYSTR("\033[A") },
	{ KEY_DOWN,		KEYSTR("\033[B") },
	{ KEY_RIGHT,		KEYSTR("\033[C") },
	{ KEY_LEFT,		KEYSTR("\033[D") },

	// Going by what xterm uses...
	{ KEY_F1,		KEYSTR("\033[OP") },
	{ KEY_F2,		KEYSTR("\033[OQ") },
	{ KEY_F3,		KEYSTR("\033[OR") },
	{ KEY_F4,		KEYSTR("\033[OS") },
	{ KEY_F5,		KEYSTR("\033[15~") },
	{ KEY_F6,		KEYSTR("\033[17~") },
	{ KEY_F7,		KEYSTR("\033[18~") },
	{ KEY_F8,		KEYSTR("\033[19~") },
	{ KEY_F9,		KEYSTR("\033[20~") },
	{ KEY_F10,		KEYSTR("\033[21~") },
	{ KEY_F11,		KEYSTR("\033[23~") },
	{ KEY_F12,		KEYSTR("\033[24~") },
#undef KEYSTR
};

struct term term = {0};

static struct xkb_context *xkb_ctx = NULL;
static struct xkb_keymap *xkb_keymap = NULL;
static struct xkb_state *xkb_state = NULL;

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
	*/
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
	unsetenv("TERMCAP");
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

	struct cell cell = term.cells[(y*term.cols)+x];

	fbc.is_inverted = !!(cell.attr & ATTR_REVERSE);
	fbc.is_inverted ^= (x == term.col && y == term.row);

	fbink_set_fg_pen_rgba(
		(cell.fg & 0x00FF0000     >> 16)& 0xFF,
		(cell.fg & 0x0000FF00     >> 8) & 0xFF,
		(cell.fg & 0x000000FF)		& 0xFF,
		0xFF,
		0, 1
	);

	fbink_set_bg_pen_rgba(
		(cell.bg & 0x00FF0000     >> 16)& 0xFF,
		(cell.bg & 0x0000FF00     >> 8) & 0xFF,
		(cell.bg & 0x000000FF)		& 0xFF,
		0xFF,
		0, 1
	);

	if (cell.c && cell.attr != ATTR_WIDEDUMMY) {
		unsigned char *c = utf8_encode(cell.c, NULL);
		fbink_print(fb, (char *)c, &fbc);
	} else fbink_print(fb, " ", &fbc);
	// fbink_grid_refresh(fb, 1, 1, &fbc);
}

static void
bellhandler(void)
{
	// Set a flag for draw.
	refresh_next = 1;
}

static void
free_xkb(void)
{
	// Free the state, if it was initialized.
	if (xkb_state) {
		xkb_state_unref(xkb_state);
		xkb_state = NULL;
	}

	// Free the keymap, if it was initialized.
	if (xkb_keymap) {
		xkb_keymap_unref(xkb_keymap);
		xkb_keymap = NULL;
	}

	// Free the context, if it was initialized.
	if (xkb_ctx) {
		xkb_context_unref(xkb_ctx);
		xkb_ctx = NULL;
	}
}

static int
setup_xkb(void)
{
	// Get a context object.
	xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!xkb_ctx)
		goto fail;

	// Load a keymap.
	// TODO: Allow names to be configured.
	xkb_keymap = xkb_keymap_new_from_names(xkb_ctx, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (!xkb_keymap)
		goto fail;

	// And finally create a state.
	xkb_state = xkb_state_new(xkb_keymap);
	if (!xkb_state)
		goto fail;

	// Initialization successful.
	return 0;

fail:
	free_xkb();
	return -1;
}

static void
handle_key(struct evdev_kbd *_, struct input_event ev)
{
#if 0
	printf("Event: %s %s %d\n",
	       libevdev_event_type_get_name(ev.type),
	       libevdev_event_code_get_name(ev.type, ev.code),
	       ev.value);
#endif

	// evdev keycodes have a fixed offset of 8.
	int code = ev.code + 8;

	// If this is a repeated key, we can consult xkb to figure out what to
	// do with it.
	// If the key is supposed to repeat then business as usual.
	if (ev.value == 2 && !xkb_keymap_key_repeats(xkb_keymap, code))
		return;

	// Check to see if the key was released (0).
	if (ev.value == 0) {
		// Tell xkb about it and return.
		xkb_state_update_key(xkb_state, code, XKB_KEY_UP);
		return;
	} else if (ev.value == 1)
		// Key was just pressed and is NOT a repeat.
		xkb_state_update_key(xkb_state, code, XKB_KEY_DOWN);

	// Check to see if this is a key that requires special handling.
	for (int i = 0; i < ARRAYLEN(string_binds); ++i) {
		// Note that this is ev.code and not code.
		if (string_binds[i].key == ev.code) {
			// Yes it is. Write the string to the pty and return.
			// TODO: Handle partial writes in the unlikely event it
			// happens.
			write(term.pty, string_binds[i].val.data, string_binds[i].val.sz);
			return;
		}
	}

	// Write the key to the pty.
	char outbuf[5] = {0}; // 4 bytes + 1 for NUL
	int n = xkb_state_key_get_utf8(xkb_state, code, outbuf, sizeof(outbuf));
	if (n == 0)
		return; // Nothing more to do.

	// TODO: Handle partial writes in the unlikely event it happens.
	write(term.pty, outbuf, n);
}

void
draw(int fb)
{
	// Write out contents of screen
	static int last_row = 0;
	static int last_col = 0;

	// Redraw the cell that the cursor was last on
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
	for (int byt = 0; byt < DAMAGE_BYTES(&term); ++byt) {
		if (!term.damage[byt])
			// No damage branch. Keep on going.
			continue;

		term_damage_t p = term.damage[byt];

		int idx = byt * DAMAGE_WIDTH; // Offset in bits
		int bit;
		while ((bit = __builtin_ffs(p)) != 0) {
			bit -= 1; // lsb is 1

			r = DAMAGE_ROW(&term, idx+bit);
			c = DAMAGE_COL(&term, idx+bit);
			draw_cell(fb, r, c);

			// Unset the bit we drew.
			p &= ~(1 << bit);
		}

		// Unmark the damage.
		term.damage[byt] = 0;
	}

	// Always draw the cursor last.
	// Might be wasting some cycles since it could have gotten drawn above,
	// but whatever.
	draw_cell(fb, term.row, term.col);

	// Handle refresh_next now.
	// This takes *a lot* of time because e-ink is slow and from what I can
	// see, fbink doesn't allow us to just say "hey refresh the screen and
	// let us know when it's done".
	// But then again, I'm tired, so there's a good chance that I am just
	// not thinking of how to do that right now.
	// Or alternatively, the way to do it is just not immediately obvious
	// to me.
	if (refresh_next) {
		refresh_next = 0;

		fbc.is_flashing = 1;
		fbink_refresh(fb, 0, 0, 0, 0, &fbc);
		fbink_wait_for_complete(fb, 0); // This is slow!
		fbc.is_flashing = 0;
	}

	term_flip(&term);
}

int
init_term(int rows, int cols, char *args[])
{
	int slave;
	if (term_init(&term, rows, cols, &slave) == -1)
		die("failed to init terminal: %s\n", strerror(errno));

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

	// Set the on_bell handler so we can flash the screen every now and
	// then.
	term.on_bell = bellhandler;

	return 0;

fail:
	if (child_pid == -1) {
		// This is the only time where it is okay to close both
		// fds.
		close(term.pty);
		close(slave);
	}

	term_free(&term);
	return -1;
}

void
readterm(void)
{
	static unsigned char buf[512] = {0};
	static int len = 0;
	int n, written;

	// Read from the pty.
	// We offset by len in case there is any data left over (probably an
	// incomplete UTF-8 sequence)
	if ((n = read(term.pty, buf+len, sizeof(buf)-len)) == -1)
		die("read: %s\n", strerror(errno));

	// Write to the terminal emulator.
	// There is potential for it to be an incomplete write, because again,
	// UTF-8.
	len += n;
	written = term_write(&term, buf, len);
	len -= written;

	// Move back if needed.
	memmove(buf, buf+written, len);
}

int
main(int argc, char *argv[])
{
	char *args[] = {"/bin/sh", NULL};//"-c", "echo \"Hello, world!\"; echo \"$$\"; sleep 1", NULL};
	char *shell = getenv("SHELL");
	if (shell) {
		args[0] = shell;
	}

	// Specified or automatically detected
	char *event_file = NULL;

	int opt;
	while ((opt = getopt(argc, argv, "e:")) != -1) {
		switch (opt) {
		case 'e': event_file = optarg; break;
		default: die("unknown flag '%c'\n", opt);
		}
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

	if (init_term(s.max_rows, s.max_cols, args) != 0)
		die("failed to init vt: %s\n", strerror(errno));

	if (setup_xkb() == -1)
		die("failed to init xkb: %s\n", strerror(errno));

	struct evdev_kbd evk = {
		.on_key = handle_key
	};
	if (evdev_init(&evk, event_file) == -1)
		die("failed to init evdev: %s\n", strerror(errno));

	struct pollfd pfds[] = {
		{ .fd = term.pty, .events = POLLIN },
		{ .fd = evk.fd, .events = POLLIN },
	};

	// Main event loop.
	// Note: writing controls whether we are waiting for more input or not.
	// To make things a little faster, we wait for stuff to finish being
	// sent to the VT before actually drawing; once the timeout is passed
	// with no data, only then do we draw.
	int rc;
	int writing = 0;
	for (;;) {
		rc = poll(pfds, sizeof(pfds)/sizeof(*pfds), writing ? draw_timeout : -1);
		if (rc == -1) {
			// EINTR is not a fatal error, and simply means that
			// the call to poll was interrupted.
			if (errno == EINTR)
				continue;
			
			// Everything else is fatal.
			break;
		}

		// Check to see if this was a timeout after data was being
		// written to the terminal.
		if (rc == 0 && writing) {
			// It was. Set timeout to infinity and draw.
			writing = 0;

#if 1
			struct timespec start, end;
			clock_gettime(CLOCK_MONOTONIC_RAW, &start);
			draw(fb);
			clock_gettime(CLOCK_MONOTONIC_RAW, &end);

			uint64_t delta_us = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_nsec - start.tv_nsec) / 1000;
			printf("draw took %ld us\n", delta_us);
#else
			draw(fb);
#endif

			// rc == 0 so there is nothing more to do.
			continue;
		}

		if (pfds[1].revents & POLLIN) {
			// Key press, probably
			if (evdev_handle(&evk) == -1) {
				perror("evdev_handle");
				break;
			}
		}

		if (pfds[0].revents & POLLIN) {
			// Activity from the pty.
			readterm();

			// More data might be coming in, so wait before
			// actually doing anything.
			writing = 1;
		}
	}

	if (rc == -1)
		perror("poll");

	// Cleanup.
	fbink_close(fb);
	free_xkb();
	evdev_free(&evk);
	term_free(&term);
}
