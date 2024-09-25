#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <libevdev/libevdev.h>

#include "evdev.h"

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

int
evdev_init(struct evdev_kbd *evk, char *eventfile)
{
	assert(evk);

	// If eventfile wasn't specified, look for the first keyboard.
	if (eventfile == NULL)
		eventfile = find_keyboard();

	// Not specified and not found
	if (eventfile == NULL) {
		errno = EINVAL;
		return -1;
	}

	fprintf(stderr, "using %s for events\n", eventfile);

	int fd = open(eventfile, O_RDONLY|O_NONBLOCK);
	if (fd == -1)
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
	if (ioctl(fd, EVIOCGRAB, &rc) == -1)
		goto fail;

	// Try to init libevdev.
	// In theory this can only fail if fd does not point to an event device.
	struct libevdev *ctx = NULL;
	if ((rc = libevdev_new_from_fd(fd, &ctx)) < 0) {
		errno = -rc;
		goto fail;
	}

	// All done.
	evk->fd = fd;
	evk->ctx = ctx;
	return 0;

fail:	; // The semicolon is intentional.

	// errno might get clobbered with the calls we're about to make
	int old_errno = errno;

	if (fd != -1)
		close(fd);

	// Restore errno and return -1
	errno = old_errno;
	return -1;
}

void
evdev_free(struct evdev_kbd *evk)
{
	assert(evk);

	close(evk->fd);
	libevdev_free(evk->ctx);
}

int
evdev_handle(struct evdev_kbd *evk)
{
	int rc;
	struct input_event ev;

	assert(evk);

	// Read events as they come in.
	do {
		rc = libevdev_next_event(evk->ctx, LIBEVDEV_READ_FLAG_NORMAL, &ev);
		if (rc == 0 && ev.type == EV_KEY)
			evk->on_key(evk, ev);
	} while (rc == 1);

	if (rc < 0)
		errno = -rc;
	return rc < 0 ? -1 : 0;
}
