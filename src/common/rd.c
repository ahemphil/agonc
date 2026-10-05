/* rd.c - buffered byte input; see rd.h.
 *
 * The buffer is a window on the file: buf[0 .. len-1] holds the file's
 * bytes from offset base, and pos is the next one to hand out, so the
 * reader's file offset is always base + pos. A seek inside the window only
 * moves pos; any other seek empties the window (len 0) at the new offset,
 * and the next read refills it from there.
 */

#include <stdio.h>
#include "rd.h"

int rd_open(struct rd *r, char *path)
{
    r->f = fopen(path, "rb");
    r->pos = 0;
    r->len = 0;
    r->base = 0;
    return r->f != NULL;
}

/* Refills the whole buffer from the file's current position, which is
 * just past the old window; returns 0 at end of file (or on an error). */
static int fill(struct rd *r)
{
    r->base = r->base + r->len;
    r->len = fread(r->buf, 1, RD_BUF, r->f);
    r->pos = 0;
    if (r->len <= 0) {
        r->len = 0;
        return 0;
    }
    return 1;
}

/* Reads forward a buffer at a time instead of seeking, so the window and
 * base stay in step with the file position without an fseek. */
int rd_open_at(struct rd *r, char *path, int offset)
{
    if (!rd_open(r, path))
        return 0;
    while (fill(r)) {
        if (offset < r->len) {
            r->pos = offset;
            return 1;
        }
        offset = offset - r->len;
    }
    return 1;                           /* the offset is at (or past) the end */
}

/* A closed reader (f NULL) reads as end of file once its buffer is used. */
int rd_getc(struct rd *r)
{
    if (r->pos >= r->len) {
        if (r->f == NULL)
            return -1;
        if (!fill(r))
            return -1;
    }
    r->pos++;
    return r->buf[r->pos - 1];
}

/* Copies straight out of the buffer rather than calling rd_getc per byte:
 * ld and cc2 read every line through here. Stops after a newline (which
 * is kept), at end of file, or when size-1 bytes are stored; the result is
 * always NUL-terminated. Each round of the outer loop copies what the
 * current window holds, so a line that crosses a buffer boundary takes
 * two rounds. */
int rd_gets(struct rd *r, char *buf, int size)
{
    int n;
    int len;
    unsigned char *p;
    unsigned char *end;
    char *q;
    unsigned char c;

    n = 0;
    size--;
    while (n < size) {
        if (r->pos >= r->len && (r->f == NULL || !fill(r)))
            break;
        len = r->len - r->pos;
        if (len > size - n)
            len = size - n;
        p = r->buf + r->pos;
        end = p + len;
        q = buf + n;
        /* len >= 1 here, so the do-while copies at least one byte */
        do {
            c = *p++;
            *q++ = (char)c;
        } while (c != '\n' && p < end);
        n = (int)(q - buf);
        r->pos = (int)(p - r->buf);
        if (c == '\n')
            break;
    }
    buf[n] = 0;
    return n;
}

/* The bytes left in the buffer (filled first if empty), at most max, for
 * the caller to use where they are: ld copies whole sections this way. */
int rd_block(struct rd *r, unsigned char **p, int max)
{
    int n;

    if (r->pos >= r->len && (r->f == NULL || !fill(r)))
        return 0;
    n = r->len - r->pos;
    if (n > max)
        n = max;
    *p = r->buf + r->pos;
    r->pos = r->pos + n;
    return n;
}

int rd_tell(struct rd *r)
{
    return r->base + r->pos;
}

/* Inside the window only pos moves; otherwise the window is emptied at
 * offset, and the next read fills from there. */
void rd_seek(struct rd *r, int offset)
{
    if (offset >= r->base && offset < r->base + r->len) {
        r->pos = offset - r->base;
        return;
    }
    fseek(r->f, offset, SEEK_SET);
    r->base = offset;
    r->len = 0;
    r->pos = 0;
}

/* ld copies short sections from all over a long library, in call order:
 * filling the whole buffer at each one would read the library several
 * times over. Reading on past len works as usual (fill). */
void rd_seek_len(struct rd *r, int offset, int len)
{
    if (offset >= r->base && offset < r->base + r->len) {
        r->pos = offset - r->base;
        return;
    }
    if (len > RD_BUF)
        len = RD_BUF;
    if (len < 1)
        len = 1;
    fseek(r->f, offset, SEEK_SET);
    r->base = offset;
    r->pos = 0;
    r->len = fread(r->buf, 1, len, r->f);
    if (r->len < 0)
        r->len = 0;
}

void rd_close(struct rd *r)
{
    if (r->f != NULL)
        fclose(r->f);
    r->f = NULL;
}
