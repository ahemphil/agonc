/* vdpbuf.c - <agon/vdp.h>'s buffered command API, VDU 23, 0, &A0,
 * bufferId; command (VDP 1.04 and later; many commands later, as the
 * header notes). In libagon.s.
 *
 * Buffers hold blocks of bytes on the VDP: data for bitmaps and samples,
 * or VDU commands to run later. IDs are 16 bits; 65535 means "this
 * buffer" inside a buffered sequence (and "all" for clear). An offset
 * that may name a block (commands 9-12) is 24 bits: with its top bit set
 * a 16-bit block number follows it (the _block functions).
 *
 * The commands whose arguments vary with their options (adjust,
 * conditions, matrices, transforms, expanding bitmaps) have AgDev's form
 * that sends the fixed start, and vdp_adv_command sends any command with
 * argument bytes the caller builds.
 *
 * Every command is VDU 23, 0, &A0, bufferId; command, then its arguments
 * (vdp.c's opening comment explains the protocol): six bytes of header,
 * the ID little-endian in bytes 3 and 4. A buffer is how a program
 * gets bulk data onto the VDP once (bitmap pixels, sound samples, fonts)
 * and how it stores a VDU sequence to replay with one short call, even
 * with conditions and jumps, so the VDP runs it without the eZ80.
 *
 * Commands that take a list of buffer IDs (copy, split, spread) end it
 * with 65535; cmd_list and cmd_array add that terminator.
 */

#include <stdarg.h>
#include <agon/vdp.h>

/* A 16-bit value, low byte first (as in vdp.c). */
#define W(b, i, v) ((b)[i] = (char)((v) & 255), (b)[(i) + 1] = (char)((v) >> 8 & 255))

/* The six bytes every buffered command starts with. */
static void head(char *b, int buffer, int command)
{
    b[0] = 23;
    b[1] = 0;
    b[2] = 0xA0;
    W(b, 3, buffer);
    b[5] = command;
}

/* A command with n (0-3) 16-bit values after it; the values past n are
 * built but not sent. */
static void cmd_words(int buffer, int command, int n, int a, int b, int c)
{
    char x[12];

    head(x, buffer, command);
    W(x, 6, a);
    W(x, 8, b);
    W(x, 10, c);
    vdu_n(x, 6 + 2 * n);
}

/* A command, then the IDs from a va_list, then 65535 (commands 13, 16, 19,
 * 21, 25, 26). first holds the 16-bit values before the list (none, or
 * one: a size or width). The caller owns va_start and va_end. Each ID
 * goes as its own two-byte vdu_n, so a list of any length needs no
 * buffer here. */
static void cmd_list(int buffer, int command, int nfirst, int first, int count, va_list ap)
{
    char x[2];
    int id;

    cmd_words(buffer, command, nfirst, first, 0, 0);
    while (count-- > 0) {
        /* Into id first: W evaluates its value twice, and va_arg there
         * would take two arguments. */
        id = va_arg(ap, int);
        W(x, 0, id);
        vdu_n(x, 2);
    }
    W(x, 0, 65535);
    vdu_n(x, 2);
}

/* The same with the IDs in an array. */
static void cmd_array(int buffer, int command, int nfirst, int first, const int *ids, int count)
{
    char x[2];
    int i;

    cmd_words(buffer, command, nfirst, first, 0, 0);
    for (i = 0; i < count; i++) {
        W(x, 0, ids[i]);
        vdu_n(x, 2);
    }
    W(x, 0, 65535);
    vdu_n(x, 2);
}

/* A command with a 24-bit offset, and a block number if with_block. The
 * offset's top bit (bit 7 of its third byte) says a block number
 * follows, so the offset itself keeps 23 bits; the block number is
 * built either way and sent only with_block. */
static void cmd_offset(int buffer, int command, long offset, int with_block, int block)
{
    char x[11];

    head(x, buffer, command);
    x[6] = (char)(offset & 255);
    x[7] = (char)(offset >> 8 & 255);
    x[8] = (char)((offset >> 16 & 127) | (with_block ? 128 : 0));
    W(x, 9, block);
    vdu_n(x, with_block ? 11 : 9);
}

/* Any buffered command: its start, then n argument bytes (two vdu_n
 * calls, so the arguments need no copying). */
void vdp_adv_command(int buffer, int command, const unsigned char *args, int n)
{
    char x[6];

    head(x, buffer, command);
    vdu_n(x, 6);
    vdu_n((const char *)args, n);
}

/* ---- writing and running ------------------------------------------------------------------- */

/* Command 0's start: the next length bytes sent go into the buffer as a
 * new block. */
void vdp_adv_write_block(int buffer, int length)
{
    cmd_words(buffer, 0, 1, length, 0, 0);
}

/* A whole block: the same and then the bytes. */
void vdp_adv_write_block_data(int buffer, int length, const char *data)
{
    vdp_adv_write_block(buffer, length);
    vdu_n(data, length);
}

/* Run the VDU commands in the buffer. */
void vdp_adv_call_buffer(int buffer)
{
    cmd_words(buffer, 1, 0, 0, 0, 0);
}

void vdp_adv_clear_buffer(int buffer)
{
    cmd_words(buffer, 2, 0, 0, 0, 0);
}

/* A writeable buffer of length bytes, zeroed. */
void vdp_adv_create(int buffer, int length)
{
    cmd_words(buffer, 3, 1, length, 0, 0);
}

/* The VDP's replies to a buffer instead of MOS (the docs advise against
 * it; VDP variables do the job now). */
void vdp_adv_stream(int buffer)
{
    cmd_words(buffer, 4, 0, 0, 0, 0);
}

/* Command 5's start: operation (0 NOT, 1 negate, 2 set, 3 add, 4 add with
 * carry, 5 AND, 6 OR, 7 XOR, plus the modifier bits) and a 16-bit offset;
 * the count, operands and arguments follow with vdu_n. */
void vdp_adv_adjust(int buffer, int operation, int offset)
{
    char x[9];

    head(x, buffer, 5);
    x[6] = operation;
    W(x, 7, offset);
    vdu_n(x, 9);
}

/* Command 6's start: run the buffer if the condition holds. operation: 0
 * non-zero, 1 zero, 2 =, 3 !=, 4 <, 5 >, 6 <=, 7 >=, 8 AND, 9 OR, plus
 * modifier bits; the value checked is at check_offset in check_buffer.
 * The operand follows with vdu_n. */
void vdp_adv_call_conditional(int buffer, int operation, int check_buffer, int check_offset)
{
    char x[11];

    head(x, buffer, 6);
    x[6] = operation;
    W(x, 7, check_buffer);
    W(x, 9, check_offset);
    vdu_n(x, 11);
}

void vdp_adv_jump_buffer(int buffer)
{
    cmd_words(buffer, 7, 0, 0, 0, 0);
}

/* Command 8's start: as vdp_adv_call_conditional, jumping. */
void vdp_adv_jump_conditional(int buffer, int operation, int check_buffer, int check_offset)
{
    char x[11];

    head(x, buffer, 8);
    x[6] = operation;
    W(x, 7, check_buffer);
    W(x, 9, check_offset);
    vdu_n(x, 11);
}

void vdp_adv_jump_offset(int buffer, long offset)
{
    cmd_offset(buffer, 9, offset, 0, 0);
}

void vdp_adv_jump_offset_block(int buffer, long offset, int block)
{
    cmd_offset(buffer, 9, offset, 1, block);
}

/* Command 10's start; the condition follows with vdu_n, as for 6. */
void vdp_adv_jump_offset_conditional(int buffer, long offset)
{
    cmd_offset(buffer, 10, offset, 0, 0);
}

void vdp_adv_jump_offset_block_conditional(int buffer, long offset, int block)
{
    cmd_offset(buffer, 10, offset, 1, block);
}

void vdp_adv_call_offset(int buffer, long offset)
{
    cmd_offset(buffer, 11, offset, 0, 0);
}

void vdp_adv_call_offset_block(int buffer, long offset, int block)
{
    cmd_offset(buffer, 11, offset, 1, block);
}

/* Command 12's start; the condition follows with vdu_n. */
void vdp_adv_call_offset_conditional(int buffer, long offset)
{
    cmd_offset(buffer, 12, offset, 0, 0);
}

void vdp_adv_call_offset_block_conditional(int buffer, long offset, int block)
{
    cmd_offset(buffer, 12, offset, 1, block);
}

/* ---- copying, splitting and spreading blocks --------------------------------------------------- */

/* The blocks of count buffers (the IDs after count) into target. */
void vdp_adv_copy_multiple(int target, int count, ...)
{
    va_list ap;

    va_start(ap, count);
    cmd_list(target, 13, 0, 0, count, ap);
    va_end(ap);
}

/* The same with the IDs in an array. */
void vdp_adv_copy_blocks(int target, const int *sources, int count)
{
    cmd_array(target, 13, 0, 0, sources, count);
}

/* All of a buffer's blocks into one. */
void vdp_adv_consolidate(int buffer)
{
    cmd_words(buffer, 14, 0, 0, 0, 0);
}

void vdp_adv_split(int buffer, int block_size)
{
    cmd_words(buffer, 15, 1, block_size, 0, 0);
}

/* Split, and spread the blocks over count target buffers (the IDs after
 * count), round and round. */
void vdp_adv_split_multiple(int buffer, int block_size, int count, ...)
{
    va_list ap;

    va_start(ap, count);
    cmd_list(buffer, 16, 1, block_size, count, ap);
    va_end(ap);
}

/* Split, one block to each buffer from target on. */
void vdp_adv_split_multiple_from(int buffer, int block_size, int target)
{
    cmd_words(buffer, 17, 2, block_size, target, 0);
}

/* Split a bitmap's rows into block_count blocks of width bytes. */
void vdp_adv_split_by_width(int buffer, int width, int block_count)
{
    cmd_words(buffer, 18, 2, width, block_count, 0);
}

void vdp_adv_split_by_width_multiple(int buffer, int width, int count, ...)
{
    va_list ap;

    va_start(ap, count);
    cmd_list(buffer, 19, 1, width, count, ap);
    va_end(ap);
}

void vdp_adv_split_by_width_multiple_from(int buffer, int width, int block_count, int target)
{
    cmd_words(buffer, 20, 3, width, block_count, target);
}

/* A buffer's blocks over count target buffers, round and round. */
void vdp_adv_spread_multiple(int buffer, int count, ...)
{
    va_list ap;

    va_start(ap, count);
    cmd_list(buffer, 21, 0, 0, count, ap);
    va_end(ap);
}

void vdp_adv_spread_multiple_from(int buffer, int target)
{
    cmd_words(buffer, 22, 1, target, 0, 0);
}

void vdp_adv_reverse_block_order(int buffer)
{
    cmd_words(buffer, 23, 0, 0, 0, 0);
}

/* Reverse the data in each block. options: 1 16-bit values, 2 32-bit, 3
 * value_size bytes each (sent), 4 within chunks of chunk_size (sent), 8
 * the block order too. n counts the bytes built, since the optional
 * values are present only when their option bits ask for them. */
void vdp_adv_reverse_block_data(int buffer, int options, int value_size, int chunk_size)
{
    char x[11];
    int n;

    head(x, buffer, 24);
    x[6] = options;
    n = 7;
    if ((options & 3) == 3) {
        W(x, n, value_size);
        n = n + 2;
    }
    if (options & 4) {
        W(x, n, chunk_size);
        n = n + 2;
    }
    vdu_n(x, n);
}

/* As vdp_adv_copy_multiple, the target referring to the sources' blocks
 * rather than copies (VDP 2.6.0). */
void vdp_adv_copy_multiple_by_reference(int target, int count, ...)
{
    va_list ap;

    va_start(ap, count);
    cmd_list(target, 25, 0, 0, count, ap);
    va_end(ap);
}

/* As vdp_adv_copy_multiple, then consolidated into one block. */
void vdp_adv_copy_multiple_consolidate(int target, int count, ...)
{
    va_list ap;

    va_start(ap, count);
    cmd_list(target, 26, 0, 0, count, ap);
    va_end(ap);
}

/* ---- the rest ---------------------------------------------------------------------------------- */

void vdp_adv_compress_buffer(int target, int source)
{
    cmd_words(target, 64, 1, source, 0, 0);
}

void vdp_adv_decompress_buffer(int target, int source)
{
    cmd_words(target, 65, 1, source, 0, 0);
}

/* Run the buffer at an event (VDP 2.12.0): 0 each frame, 1 a mode
 * change, 2 a key, 3 the mouse, 4 a palette change, 5 a pixel read (2-5:
 * VDP 2.15.0). */
void vdp_adv_set_callback(int buffer, int event)
{
    cmd_words(buffer, 80, 1, event, 0, 0);
}

/* 65535 for the event: every event; 65535 for the buffer: every buffer. */
void vdp_adv_remove_callback(int buffer, int event)
{
    cmd_words(buffer, 81, 1, event, 0, 0);
}

/* The buffer's details to the VDP's USB serial console. */
void vdp_adv_debug_info(int buffer)
{
    cmd_words(buffer, 128, 0, 0, 0, 0);
}
