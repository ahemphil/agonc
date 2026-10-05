/* rd.h - buffered byte input for the passes.
 *
 * Reads a file through a 4 KB buffer filled with fread, so a pass makes
 * one MOS call per 4 KB instead of one per byte. Under AgDev this also
 * sidesteps its fgetc, which treats a 0x00 or 0xFF byte as end of file
 * (see io.h): fread is a single mos_fread whose byte count is exact.
 *
 * rd_tell and rd_open_at let a reader close a file and resume it later
 * (cpp does this around #include, so only one input is ever open); the
 * resume skips by whole buffers rather than seeking. rd_seek repositions
 * an open file (ld's copy pass, cc2's second pass); rd_seek_len, for ld,
 * reads only the bytes a short section needs instead of a whole buffer.
 */

#ifndef RD_H
#define RD_H

#include <stdio.h>

#define RD_BUF 4096

struct rd {
    FILE *f;            /* NULL once closed (or if the open failed) */
    int pos;            /* the next byte to return, an index into buf */
    int len;            /* how many bytes of buf are valid */
    int base;           /* the file offset of buf[0] */
    unsigned char buf[RD_BUF];
};

int rd_open(struct rd *r, char *path);     /* 1 on success */
int rd_open_at(struct rd *r, char *path, int offset);  /* open, then skip offset bytes; 1 on success */
int rd_getc(struct rd *r);                 /* next byte 0-255, or -1 at end of file */
int rd_gets(struct rd *r, char *buf, int size);  /* like fgets; returns the byte count, 0 at end of file */
int rd_block(struct rd *r, unsigned char **p, int max);  /* up to max bytes in place at *p; 0 at end of file */
int rd_tell(struct rd *r);                 /* the offset of the next byte rd_getc returns */
void rd_seek(struct rd *r, int offset);    /* within the buffer if possible, else fseek */
void rd_seek_len(struct rd *r, int offset, int len);  /* the same, reading len bytes (at most RD_BUF) if it must read */
void rd_close(struct rd *r);

#endif
