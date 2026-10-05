/* setjmp.h - non-local jumps (C89 4.6), implemented in rt.s. A jmp_buf
 * holds setjmp's return address, frame pointer (IX) and stack pointer.
 * Generated code keeps no local in a register from one statement to the
 * next, so after a longjmp every local has its last stored value, whether
 * it is volatile or not. */

#ifndef _SETJMP_H
#define _SETJMP_H

typedef int jmp_buf[3];

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);

#endif
