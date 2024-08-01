#ifndef UTF8_H
#define UTF8_H

#include <stdint.h>

typedef uint32_t rune;

/** Decodes one utf8 rune at the start of buf.
 * 
 * The return value indicates how many bytes from buf that were used by the
 * function.
 * The zero value indicates that there is currently not enough data in the
 * buffer to fully decode the rune.
 * Any value other than 0 can be seen as a successful decode.
 *
 * An assertion is raised if buf is null or out is null.
 */
int utf8_decode(unsigned char *buf, size_t n, rune *out);

/** Encodes a rune into UTF-8 at out.
 * The return value is the number of bytes that would have been written,
 * regardless of if they have been written or not.
 *
 * If n is greater than 0, then out must be valid.
 */
size_t utf8_encodeto(rune r, unsigned char *out, size_t n);

/** Encodes a rune into UTF-8 into an internal buffer and returns it.
 * The return value is never NULL.
 *
 * If out is supplied, it is the number of bytes that should be read from buf.
 *
 * This function is not thread safe.
 */
unsigned char *utf8_encode(rune r, size_t *out);

#endif /* UTF8_H */
