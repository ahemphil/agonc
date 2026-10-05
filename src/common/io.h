/* io.h - output helpers shared by every pass.
 *
 * The passes never call fputs: AgDev 3.1.0's fputs appends a newline after
 * the string, like puts (its lib/agon/fputs.c.src calls fputc(10) on
 * reaching the NUL), which would make stage-1 output differ from the host's
 * and from stage 2's. out_str writes exactly the string's bytes, as one
 * fwrite (a single mos_fwrite block under AgDev, rather than a MOS call per
 * byte). build.py's lint rejects fputs in pass sources.
 *
 * Input has a related AgDev 3.1.0 limitation: its fgetc (and so fgets)
 * treats a 0x00 or 0xFF byte as end of file, ignoring MOS's own EOF flag.
 * Every pass therefore reads its inputs through common/rd.c (fread, 4 KB
 * at a time), which is also one MOS call per buffer instead of per byte.
 * Only args.c's response files still use fgetc; they are short text.
 */

#ifndef IO_H
#define IO_H

#include <stdio.h>

void out_str(FILE *f, char *s);

#endif
