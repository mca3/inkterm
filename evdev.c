#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <linux/input.h>
#include <sys/file.h>
#include <sys/types.h>

#include <libevdev/libevdev.h>
#include <xkbcommon/xkbcommon.h>

#include "evdev.h"
#include "main.h"
#include "term.h"
#include "x.h"

int evdev_fd = -1;

static struct libevdev *evdev_ctx = NULL;
static struct xkb_context *xkb_ctx = NULL;
static struct xkb_keymap *xkb_keymap = NULL;
static struct xkb_state *xkb_state = NULL;

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

/* For some backstory on this file, because why not: a very large switch
 * statement used to be here because I didn't know of any other way immediately
 * to go from evdev keycodes to whatever needs to go into the console.
 * Thankfully, I eventually did a lot of research and figured out that I can
 * replace everything with xkbcommon, like probably everything else does.
 *
 * I don't really want to introduce even more dependencies, but this saves me
 * more time in the long run and makes it much easier for end-users to
 * configure.
 */

/* Opens the event device at evpath to determine if it is a keyboard or not.
 * If an error occurs, errno is set. */
static int
is_keyboard(char *evpath)
{
	struct libevdev *evdev = NULL;
	int is_kbd = -1;
	int rc;
	int fd = open(evpath, O_RDONLY|O_NONBLOCK);
	if (fd == -1)
		return -1;

	// Try to init libevdev.
	// In theory this can only fail if fd does not point to an event device.
	if ((rc = libevdev_new_from_fd(fd, &evdev)) < 0) {
		errno = -rc;
		goto fail;
	}

	// Keyboards should have both EV_KEY and EV_REP.
	is_kbd = libevdev_has_event_type(evdev, EV_KEY) &&
		 libevdev_has_event_type(evdev, EV_REP);
	if (!is_kbd)
		goto fail;

	// Additionally, keyboards should have keys.
	// FBInk seems like it tests for codes 1-32, so let's do the same
	// thing. (See input-event-codes.h in Linux headers.)
	for (int i = 1; i <= 32; ++i) {
		if ((is_kbd = !!libevdev_has_event_code(evdev, EV_KEY, i)) == 0)
			goto fail;
	}

fail:
	if (fd)
		close(fd);
	if (evdev)
		libevdev_free(evdev);
	return is_kbd;
}

/* Finds the first keyboard input device. */
static char *
find_keyboard(void)
{
	static char buf[512] = {0}; // Far more than enough
	DIR *dir;
	struct dirent *dirent;

	dir = opendir("/dev/input");
	if (dir == NULL)
		return NULL;

	while ((dirent = readdir(dir)) != NULL) {
		snprintf(buf, sizeof(buf), "/dev/input/%s", dirent->d_name);
		if (is_keyboard(buf) == 1)
			goto done;
	}

done:
	closedir(dir);
	return buf;
}

static void
handle_key(struct input_event ev)
{
	printf("Event: %s %s %d\n",
	       libevdev_event_type_get_name(ev.type),
	       libevdev_event_code_get_name(ev.type, ev.code),
	       ev.value);

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

int
evdev_init(char *eventfile)
{
	// If eventfile wasn't specified, look for the first keyboard.
	if (eventfile == NULL)
		eventfile = find_keyboard();

	// Not specified and not found
	if (eventfile == NULL) {
		errno = EINVAL;
		return -1;
	}

	fprintf(stderr, "using %s for events\n", eventfile);

	// Make sure we don't accidentially initialize ourselves again.
	if (evdev_ctx)
		die("attempted to re-initialize evdev");

	evdev_fd = open(eventfile, O_RDONLY|O_NONBLOCK);
	if (evdev_fd == -1)
		return -1;

	// TODO: I don't think that this really is a failure case and it may
	// actually be covered by EVIOCGRAB.
	// But I need to investigate this more, so I'll just comment it out for
	// now.
	// if (flock(evdev_fd, LOCK_EX) == -1)
	// 	die("failed to get exclusive lock for evdev: %s\n", strerror(errno));

	// TODO: libevdev_grab
	// That page also says that grabbing is generally not a good idea for
	// most cases, and I agree; on e-readers there usually is no VT and
	// when inkterm is running, nothing else should be other than itself,
	// i.e. no Nickel, no KOReader, no Plato, and so on.
	// However, it makes it easier to test on a desktop.
	int rc = 1; /* ioctl wants a pointer to an integer */
	if (ioctl(evdev_fd, EVIOCGRAB, &rc) == -1)
		goto fail;

	// Try to init libevdev.
	// In theory this can only fail if fd does not point to an event device.
	if ((rc = libevdev_new_from_fd(evdev_fd, &evdev_ctx)) < 0) {
		errno = -rc;
		goto fail;
	}

	// TODO: Check to see if this is a keyboard or not.

	// Now that the first half of the ceremony is done, we now need to
	// setup xkbcommon.

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

	// All done.
	return 0;

fail:	; // The semicolon is intentional.

	// errno might get clobbered with the calls we're about to make
	int old_errno = errno;

	// evdev_free takes care of everything
	evdev_free();

	// Restore errno and return -1
	errno = old_errno;
	return -1;
}

void
evdev_free(void)
{
	// Close the file.
	if (evdev_fd != -1) {
		close(evdev_fd);
		evdev_fd = -1;
	}

	// Free the evdev struct.
	if (evdev_ctx) {
		libevdev_free(evdev_ctx);
		evdev_ctx = NULL;
	}

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

int
evdev_handle(void)
{
	int rc;
	struct input_event ev;

	assert(evdev_ctx);

	// Read events as they come in.
	do {
		rc = libevdev_next_event(evdev_ctx, LIBEVDEV_READ_FLAG_NORMAL, &ev);
		if (rc == 0 && ev.type == EV_KEY)
			handle_key(ev);
	} while (rc == 1);

	if (rc < 0)
		errno = -rc;
	return rc < 0 ? -1 : 0;
}
