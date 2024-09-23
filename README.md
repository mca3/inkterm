# inkterm

inkterm is a terminal emulator for Kobo devices.
(Though I have only tested it on the Libra 2.)

The terminal emulator itself is in a somewhat working state with a couple of known issues, and input is "supported" but there is no on-screen keyboard so you'll have to bring your own `/dev/input/eventN` device.

## Build

To build inkterm, make sure all submodules and dependencies are satisfied and then run `make` to generate a static binary.
Asides from the usual build tools, you will need:

- byacc
- meson
- ninja (or samurai)

There is currently no launch script and I have been primarily been testing it on a Linux machine, but if you are feeling adventurous then you can setup an [Alpine Linux](https://alpinelinux.org) rootfs and compile the project yourself so you can launch it remotely.
