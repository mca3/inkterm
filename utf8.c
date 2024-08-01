#include <assert.h>
#include <string.h>

#include "utf8.h"

/* Determines how many bytes a rune occupies. */
static inline size_t
runesize(rune r)
{
	// There is probably a more clever way to write this.
	if	(r <=     0x7F) return 1;
	else if (r <=    0x7FF) return 2;
	else if (r <=   0xFFFF) return 3;
	else if (r <= 0x10FFFF) return 4;

	// The rune is invalid if we get here!
	return 0;
}

int
utf8_decode(unsigned char *buf, size_t n, rune *out)
{
	// Not really sure if this is "proper" and "spec" "compliant", but it
	// works on the input I throw at it.

	// Nothing can ever be successful if n <= 0.
	if (n <= 0)
		return -1;

	// Do sanity checks.
	assert(buf);
	assert(out);

	// Determine the length of the rune and decode the first byte at the
	// same time.
	// There's probably a better way to write this!
	int sz = 0;
	rune r = 0;
	if ((*buf & 0x80) == 0x00) {
		// Special case.
		// 0x00-0x7F is just plain ASCII.
		*out = buf[0];
		return 1;
	} else if ((*buf & 0xE0) == 0xC0) { // 0b110 (2 bytes)
		sz = 2;
		r = *buf & ~0xE0;
	} else if ((*buf & 0xF0) == 0xE0) { // 0b1110 (3 bytes)
		sz = 3;
		r = *buf & ~0xF0;
	} else if ((*buf & 0xF8) == 0xF0) { // 0b11110 (4 bytes)
		sz = 4;
		r = *buf & ~0xF8;
	} else {
		// Invalid byte or unexpected continuation byte.
		*out = 0xFFFD;
		return 1;
	}

	// Make sure that we can read the rest of the byte.
	// If we can't, return 0 to signal that we have not read anything, but
	// that the input can still be valid.
	if (n < sz)
		return 0;

	// Decode the first byte, and then do the rest of the continuation
	// bytes.
	for (int i = 1; i < sz; ++i) {
		unsigned char b = buf[i];
		if ((b & 0xC0) != 0x80) { // 0b10XXXXXX
			// Invalid byte, we expected a continuation byte.
			// Replace the rune we are decoding with an error byte.
			*out = 0xFFFD;
			return i;
		}

		// Shift in the new bytes.
		r <<= 6;
		r |= b & ~0xC0;
	}

	// Successful decoding.
	*out = r;
	return sz;
}

size_t
utf8_encodeto(rune r, unsigned char *out, size_t n)
{
	// Again, probably a far more clever way to write this...
	
	// Make sure that the output buffer is valid if n is greater than 0.
	if (n > 0) assert(out);

	size_t sz = runesize(r);
	if (sz == 1) {
		// It's likely safe to just put it there as is.
		*out = r & 0xFF;
		return 1;
	} else if (sz == 0) {
		// Invalid rune
		return 0;
	}

	// More than one byte unfortunately.
	for (int i = sz-1; i > 0; --i) {
		if (i > n-1) {
			// Don't do OOB writes.
			r >>= 6;
			continue;
		}

		// Write a continuation byte.
		out[i] = (r & 0x3F) | 0x80;
		r >>= 6;
	}

	// What remains is our start byte.
	switch (sz) {
	case 1: /* unreachable */  break;
	case 2: out[0] = r | 0xC0; break;
	case 3: out[0] = r | 0xE0; break;
	case 4: out[0] = r | 0xF0; break;
	}

	return sz;
}

unsigned char *
utf8_encode(rune r, size_t *out)
{
	static unsigned char buf[5] = {0};

	// Clear out the buffer and overwrite its contents.
	memset(buf, 0, sizeof(buf));
	size_t sz = utf8_encodeto(r, buf, sizeof(buf)-1);

	// Set out to sz if it is set.
	if (out)
		*out = sz;

	return buf;
}
