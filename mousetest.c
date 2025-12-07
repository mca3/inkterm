#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

static struct termios old_termios;

static void
enter_raw_mode(void)
{
	struct termios raw;
	tcgetattr(STDIN_FILENO, &raw);
	old_termios = raw;
	raw.c_lflag &= ~(ECHO | ICANON);
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static void
exit_raw_mode(void)
{
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_termios);
}

static void
mouse_click(void)
{
	int c = 0;

	// read garbage
	read(STDIN_FILENO, &c, 1);
	read(STDIN_FILENO, &c, 1);
	
	//putchar(c);

	int btn = 0, x = 0, y = 0;
	
	while (read(STDIN_FILENO, &c, 1) == 1 && c != ';') {
		btn *= 10;		
		btn += c - '0';
	}

	while (read(STDIN_FILENO, &c, 1) == 1 && c != ';') {
		y *= 10;		
		y += c - '0';
	}

	while (read(STDIN_FILENO, &c, 1) == 1 && c != 'M' && c != 'm') {
		x *= 10;		
		x += c - '0';
	}

	printf("\x1b[2J\x1b[%d;%dHX", x, y);
	fflush(stdout);
}

int
main(void)
{
	atexit(exit_raw_mode);

	enter_raw_mode();

	printf("\x1b[?1000;1002;1006h");
	fflush(stdout);

	int c = 0;
	while (read(STDIN_FILENO, &c, 1) == 1) {
		if (c == '\x1b')
			mouse_click();
	}

	printf("\x1b[?1000;1002;1006l");
	fflush(stdout);
}
