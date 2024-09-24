#ifndef EVDEV_H
#define EVDEV_H

#include <libevdev/libevdev.h>

/** File descriptor that evdev is reading from.
 * Set to be non-blocking. */
extern int evdev_fd;

/** Initializes evdev and xkbcommon to read keyboard events from eventfile.
 *
 * Returns -1 on error.
 */
int evdev_init(char *eventfile);

/** Frees evdev and xkbcommon stuff. */
void evdev_free(void);

/** Handles evdev events as they come in and writes keys to the terminal pty.
 *
 * Returns -1 on error and sets errno. */
int evdev_handle(void);

#endif /* EVDEV_H */
