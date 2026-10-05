# stage1/makefile - AgDev builds one pass of the bootstrap compiler.
#
# The top-level `make stage1` builds all four passes (cpp, cc1, cc2, ld)
# into build/stage1/. For each it assembles an ordinary AgDev project,
# build/stage1/<pass>/, whose src/ holds a copy of src/<pass>/ and
# src/common/, and runs
#
#     make -C build/stage1/<pass> -f <this file> NAME=<pass>
#
# The passes link AgDev's own C library and runtime. Nothing they produce
# contains AgDev code, which is what lets the compiler rebuild itself from
# them.

NAME ?= cc1
DESCRIPTION = "agonc $(NAME) (stage 1)"
COMPRESSED = NO

CFLAGS = -Wall -Wextra -Werror -Wno-unused-parameter -Oz

# The passes keep their tables in fixed arrays, so they need much more
# bss than AgDev's default split gives: 160 KB of image, 256 KB of
# bss and heap, and 32 KB of stack.
BSSHEAP_LOW = 068000
BSSHEAP_HIGH = 0A7FFF
STACK_HIGH = 0AFFFF

# AgDev's default exit handler prints "Okay" and returns 0 to MOS whatever
# the status, which would hide every failing pass from MOS and the driver.
LDHAS_EXIT_HANDLER = 0

include $(shell cedev-config --makefile)
