#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <linux/input.h>
#include <sys/file.h>

#include <libevdev/libevdev.h>
#include <xkbcommon/xkbcommon.h>

#include "vt100.h"
#include "x.h"
#include "evdev.h"

int evdev_fd = -1;

static struct libevdev *evdev_ctx;
static struct xkb_context *xkb_ctx;
static struct xkb_keymap *xkb_keymap;
static struct xkb_state *xkb_state;

#define MOD_SHIFT (1 << 0)
#define MOD_ALT   (1 << 1)
#define MOD_CTRL  (1 << 2)

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

static void
handle_key(struct input_event ev)
{
	printf("Event: %s %s %d\n",
	       libevdev_event_type_get_name(ev.type),
	       libevdev_event_code_get_name(ev.type, ev.code),
	       ev.value);

	// evdev keycodes have a fixed offset of 8.
	ev.code += 8;

	// If this is a repeated key, we can consult xkb to figure out what to
	// do with it.
	// If the key is supposed to repeat then business as usual.
	if (ev.value == 2 && !xkb_keymap_key_repeats(xkb_keymap, ev.code))
		return;

	// Check to see if the key was released (0).
	if (ev.value == 0) {
		// Tell xkb about it and return.
		xkb_state_update_key(xkb_state, ev.code, XKB_KEY_UP);
		return;
	} else if (ev.value == 1)
		// Key was just pressed and is NOT a repeat.
		xkb_state_update_key(xkb_state, ev.code, XKB_KEY_DOWN);

	// Write the key to the pty.
	char outbuf[5] = {0}; // 4 bytes + 1 for NUL
	int n = xkb_state_key_get_utf8(xkb_state, ev.code, outbuf, sizeof(outbuf));
	if (n == 0)
		return; // Nothing more to do.

	// TODO: Handle partial writes in the unlikely event it happens.
	write(term.pty, outbuf, n);
}

int
evdev_init(char *eventfile)
{
	// This argument must be specified.
	assert(eventfile);

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

void
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
}
