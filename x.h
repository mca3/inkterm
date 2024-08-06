#ifndef X_H
#define X_H

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define ARRAYLEN(arr) (sizeof((arr))/sizeof(*(arr)))

static inline void *
xmalloc(size_t n)
{
	void *ptr = malloc(n);
	assert(ptr);
	memset(ptr, 0, n);
	return ptr;
}

static inline void
die(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

#endif /* ifndef X_H */
