/* int24.h - 24-bit integer semantics, identical on every build.
 *
 * The passes build with AgDev and their own compiler (int is 24 bits) and
 * with a host compiler (int is 32). Every value a pass reads from a file or
 * folds is defined as 24-bit two's complement, so it goes through wrap24:
 * the identity on the target, and on the host exactly what the target's
 * arithmetic would have produced. Without it the host and stage-1 passes
 * printed the same constant differently (15790320 vs -986896), which would
 * break the stage2 == stage3 comparison.
 */

#ifndef INT24_H
#define INT24_H

int wrap24(int v);

#endif
