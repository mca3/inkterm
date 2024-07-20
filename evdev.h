#ifndef EVDEV_H
#define EVDEV_H

#include <libevdev/libevdev.h>

/** Handles evdev events as they come in and writes keys to the terminal pty.
 * If characters have been written, then this function will return 1.
 * Otherwise, it will return 0.
 *
 * dev must not be NULL.
 */
int evdev_handle(struct libevdev *dev);

#endif /* EVDEV_H */
