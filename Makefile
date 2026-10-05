# Makefile - build agonc, the Agon C compiler. See BUILDING.md.
#
#   make stage1     AgDev builds the bootstrap compiler     -> build/stage1/
#   make sdcard     the SD-card tree for building agonc on an Agon
#                   (stage 1, sources, bootstrap scripts)   -> build/sdcard/
#   make emulator   start fab-agon-emulator on build/sdcard/
#
#   make host       agonc for this computer                 -> build/host/
#   make cross      agonc for this computer builds agonc for the Agon,
#                   as a ready-to-copy SD-card tree         -> build/agon/
#   make lint       check the sources stay inside the language they are
#                   written in (needs Python)
#   make check      every test suite (needs Python and the emulator)
#   make visual     the VDU tour in the windowed emulator, for a person to
#                   look at (tests/visual/vdu_tour.c)
#   make manual     the user manual as a PDF for printing (needs pandoc,
#                   Typst and Python)                       -> build/manual/
#   make clean      remove build/
#
# Tool locations are in config.mk; put your own in config.local.mk.

-include config.local.mk
include config.mk

PASSES := cpp cc1 cc2 ld
PROGS := $(PASSES) agonc
LIBC_S := $(foreach u,ctype malloc stdio stdlib string exit time mos,build/cross/$(u).s)
LIBM_S := $(foreach u,fp math ll,build/cross/$(u).s)
LIBAGON_UNITS := uart vdp mosapi sysvar vdpsys vdpbmp vdpaudio vdpbuf vdpmore handler
LIBAGON_S := $(foreach u,$(LIBAGON_UNITS),build/cross/$(u).s) lib/agon/kbint.s

HOST_CFLAGS := -std=c89 -pedantic -Wall -Wextra -Werror -Wdeclaration-after-statement -Wshadow \
               -Wno-unused-parameter -fsigned-char -O1 -g

# ---- the commands recipes use: cmd.exe on Windows, sh elsewhere ----------------

empty :=
space := $(empty) $(empty)

ifeq ($(OS),Windows_NT)
SHELL := cmd.exe
PATHSEP := ;
CD := cd /d
native = $(subst /,\,$1)
MKDIR = if not exist $(call native,$1) mkdir $(call native,$1)
RMDIR = if exist $(call native,$1) rmdir /s /q $(call native,$1)
CP = copy /y $(call native,$1) $(call native,$2) >nul
CPTREE = xcopy /e /i /q /y $(call native,$1) $(call native,$2) >nul
CAT = copy /b $(subst $(space),+,$(call native,$1)) $(call native,$2) >nul
else
PATHSEP := :
CD := cd
native = $1
MKDIR = mkdir -p $1
RMDIR = rm -rf $1
CP = cp $1 $2
CPTREE = mkdir -p $2 && cp -R $1/. $2
CAT = cat $1 > $2
endif

.PHONY: all stage1 sdcard emulator host cross lint check visual manual clean
all: sdcard

# ---- stage 1: AgDev builds cpp, cc1, cc2 and ld ------------------------------
#
# Each pass is an ordinary AgDev project, build/stage1/<pass>/, whose src/ is
# a copy of src/<pass>/ and src/common/; stage1/makefile is its makefile.

define stage1_pass
	$(call MKDIR,build/stage1/$1/src)
	$(call CP,src/$1/*,build/stage1/$1/src/)
	$(call CP,src/common/*,build/stage1/$1/src/)
	$(MAKE) -C build/stage1/$1 -f $(CURDIR)/stage1/makefile NAME=$1
	$(call CP,build/stage1/$1/bin/$1.bin,build/stage1/$1.bin)
endef

stage1:
	$(call stage1_pass,cpp)
	$(call stage1_pass,cc1)
	$(call stage1_pass,cc2)
	$(call stage1_pass,ld)

# ---- the SD-card trees ---------------------------------------------------------
#
# Both put the headers and runtime in /lib and ez80asm in /bin; `lib_tree`
# does that part.

define lib_tree
	$(call MKDIR,$1/bin/agonc)
	$(call MKDIR,$1/lib/agon)
	$(call MKDIR,$1/usrlib)
	$(call MKDIR,$1/mos)
	$(call CP,third_party/ez80asm/ez80asm.bin,$1/bin/ez80asm.bin)
	$(call CP,lib/libc/*.h,$1/lib/)
	$(call CP,lib/agon/*.h,$1/lib/agon/)
	$(call CP,lib/rt/crt0.s,$1/lib/crt0.s)
	$(call CP,lib/rt/rt.s,$1/lib/rt.s)
endef

# build/sdcard: the bootstrap card. Stage 1 in /bin/agonc, and under /agonc
# the sources and bootstrap/, whose four MOS scripts build the rest on the
# Agon (BUILDING.md).
SD := build/sdcard

sdcard: stage1
	$(call RMDIR,$(SD))
	$(call lib_tree,$(SD))
	$(call CP,build/stage1/cpp.bin,$(SD)/bin/agonc/cpp.bin)
	$(call CP,build/stage1/cc1.bin,$(SD)/bin/agonc/cc1.bin)
	$(call CP,build/stage1/cc2.bin,$(SD)/bin/agonc/cc2.bin)
	$(call CP,build/stage1/ld.bin,$(SD)/bin/agonc/ld.bin)
	$(call CPTREE,src,$(SD)/agonc/src)
	$(call CPTREE,lib,$(SD)/agonc/lib)
	$(call CPTREE,tools,$(SD)/agonc/tools)
	$(call CPTREE,bootstrap,$(SD)/agonc/bootstrap)
	$(call MKDIR,$(SD)/agonc/out)
	$(call MKDIR,$(SD)/agonc/s2)
	$(call MKDIR,$(SD)/agonc/s3)
	$(call MKDIR,$(SD)/agonc/licenses)
	$(call CP,third_party/ez80asm/LICENSE,$(SD)/agonc/licenses/ez80asm.txt)

# The emulator runs from its own folder, where it finds its firmware.
emulator:
	$(CD) $(call native,$(dir $(EMULATOR))) && $(call native,$(EMULATOR)) --sdcard $(call native,$(CURDIR)/$(SD)) --mos $(call native,$(MOS_ROM)) $(EMULATOR_FLAGS)

# ---- the compiler for this computer --------------------------------------------

define host_prog
	$(call native,$(HOSTCC)) $(HOST_CFLAGS) -Isrc/common -o build/host/$1$(EXE) $(wildcard src/$1/*.c) $(wildcard src/common/*.c)
endef

host:
	$(call MKDIR,build/host)
	$(call host_prog,cpp)
	$(call host_prog,cc1)
	$(call host_prog,cc2)
	$(call host_prog,ld)
	$(call host_prog,agonc)

# ---- the Agon compiler, built on this computer ---------------------------------
#
# build/agon is also the host driver's AGONC_ROOT (its /lib and /tmp). The
# library's units are compiled with -c and joined into libc.s, and fp and
# math into libm.s, which the driver adds for floating point; then each
# program is built from the same response file the Agon bootstrap uses.
# The results are byte-identical to the Agon's own stage 2 (`make check`).

AGON := build/agon
DRIVER := $(call native,build/host/agonc$(EXE))
export AGONC_ROOT := $(AGON)
export HOSTCC
export PATH := $(call native,$(HOST_EZ80ASM_DIR))$(PATHSEP)$(PATH)

cross: host
	$(call RMDIR,$(AGON))
	$(call RMDIR,build/cross)
	$(call lib_tree,$(AGON))
	$(call MKDIR,$(AGON)/tmp)
	$(call MKDIR,build/cross)
	$(DRIVER) -O -Werror -c -o build/cross/ctype.s lib/libc/ctype.c
	$(DRIVER) -O -Werror -c -o build/cross/malloc.s lib/libc/malloc.c
	$(DRIVER) -O -Werror -c -o build/cross/stdio.s lib/libc/stdio.c
	$(DRIVER) -O -Werror -c -o build/cross/stdlib.s lib/libc/stdlib.c
	$(DRIVER) -O -Werror -c -o build/cross/string.s lib/libc/string.c
	$(DRIVER) -O -Werror -c -o build/cross/exit.s lib/libc/exit.c
	$(DRIVER) -O -Werror -c -o build/cross/time.s lib/libc/time.c
	$(DRIVER) -O -Werror -c -o build/cross/fp.s lib/libc/fp.c
	$(DRIVER) -O -Werror -c -o build/cross/math.s lib/libc/math.c
	$(DRIVER) -O -Werror -c -o build/cross/ll.s lib/libc/ll.c
	$(DRIVER) -O -Werror -c -o build/cross/mos.s lib/agon/mos.c
	$(DRIVER) -O -Werror -c -o build/cross/uart.s lib/agon/uart.c
	$(DRIVER) -O -Werror -c -o build/cross/vdp.s lib/agon/vdp.c
	$(DRIVER) -O -Werror -c -o build/cross/mosapi.s lib/agon/mosapi.c
	$(DRIVER) -O -Werror -c -o build/cross/sysvar.s lib/agon/sysvar.c
	$(DRIVER) -O -Werror -c -o build/cross/vdpsys.s lib/agon/vdpsys.c
	$(DRIVER) -O -Werror -c -o build/cross/vdpbmp.s lib/agon/vdpbmp.c
	$(DRIVER) -O -Werror -c -o build/cross/vdpaudio.s lib/agon/vdpaudio.c
	$(DRIVER) -O -Werror -c -o build/cross/vdpbuf.s lib/agon/vdpbuf.c
	$(DRIVER) -O -Werror -c -o build/cross/vdpmore.s lib/agon/vdpmore.c
	$(DRIVER) -O -Werror -c -o build/cross/handler.s lib/agon/handler.c
	$(call CAT,$(LIBC_S),$(AGON)/lib/libc.s)
	$(call CAT,$(LIBM_S),$(AGON)/lib/libm.s)
	$(call CAT,$(LIBAGON_S),$(AGON)/lib/libagon.s)
	$(DRIVER) --index $(AGON)/lib/crt0.s $(AGON)/lib/rt.s $(AGON)/lib/libc.s $(AGON)/lib/libm.s $(AGON)/lib/libagon.s
	$(DRIVER) -o $(AGON)/bin/agonc/cpp.bin @bootstrap/cpp.rsp
	$(DRIVER) -o $(AGON)/bin/agonc/cc1.bin @bootstrap/cc1.rsp
	$(DRIVER) -o $(AGON)/bin/agonc/cc2.bin @bootstrap/cc2.rsp
	$(DRIVER) -o $(AGON)/bin/agonc/ld.bin @bootstrap/ld.rsp
	$(DRIVER) -o $(AGON)/mos/agonc.bin @bootstrap/agonc.rsp

# ---- checks ----------------------------------------------------------------------

lint:
	$(call native,$(PYTHON)) tests/tools/lint.py

check: lint sdcard cross
	$(call native,$(PYTHON)) tests/run_all.py

visual: cross
	$(call native,$(PYTHON)) tests/visual/run_tour.py

manual:
	$(call native,$(PYTHON)) docs/manual/build.py $(call native,$(PANDOC)) $(call native,$(TYPST))

clean:
	$(call RMDIR,build)
