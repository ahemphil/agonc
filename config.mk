# config.mk - where the Makefile finds the tools it uses.
#
# Every setting here is a default: override it on the command line
# (make HOSTCC=gcc host) or, for good, in config.local.mk beside this file,
# which is not in git and is read first. See BUILDING.md.
#
# AgDev itself is not set here: its bin/ folder must be on your PATH (as
# AgDev's own instructions say), because its makefiles run cedev-config.

ifeq ($(OS),Windows_NT)
EXE := .exe
else
EXE :=
endif

# A C89 compiler for this computer: make host, make cross.
HOSTCC ?= cc

# The folder holding ez80asm built for this computer: make cross.
HOST_EZ80ASM_DIR ?= $(CURDIR)/third_party/bin

# Python 3: make lint, make check.
PYTHON ?= python3

# fab-agon-emulator and the MOS firmware it boots: make emulator, make check.
EMULATOR ?= $(CURDIR)/third_party/emulator/fab-agon-emulator$(EXE)
MOS_ROM ?= $(CURDIR)/third_party/mos/MOS-2.3.3.bin

# Extra options for make emulator; -u runs the eZ80 as fast as the host can
# (drop it to run at the real 18.432 MHz).
EMULATOR_FLAGS ?= --firmware console8 -u

# pandoc and Typst: make manual.
PANDOC ?= pandoc
TYPST ?= typst
