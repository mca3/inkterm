#ifndef EVDEV_H
#define EVDEV_H

#include <libevdev/libevdev.h>

struct evdev_kbd {
	/** File descriptor that evdev is reading from.
	 * Set to be non-blocking. */
	int fd;

	/** evdev context for fd */
	struct libevdev *ctx;

	/** on key handler */
	void (*on_key)(struct evdev_kbd *evk, struct input_event ev);
};

/** Initializes evdev and xkbcommon to read keyboard events from eventfile.
 *
 * Returns -1 on error.
 */
int evdev_init(struct evdev_kbd *evk, char *eventfile);

/** Frees evdev and xkbcommon stuff. */
void evdev_free(struct evdev_kbd *evk);

/** Handles evdev events as they come in and writes keys to the terminal pty.
 *
 * Returns -1 on error and sets errno. */
int evdev_handle(struct evdev_kbd *evk);

#endif /* EVDEV_H */
