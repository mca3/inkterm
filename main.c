#include <stdio.h>
#include <unistd.h>

#include "vt100.h"

int
main(int argc, char *argv[])
{
	char *args[] = {"/bin/sh", "-c", "echo hello", NULL};
	vt100_init(40, 20, "/bin/sh", args);

	// TODO: The rest of the owl.
	// For now, just shuffle between the pty and stdout
	static char buf[512] = {0};
	int n;
	while ((n = read(term.pty, buf, sizeof(buf))) != -1) {
		write(STDOUT_FILENO, buf, n);
	}

	vt100_free();
}
