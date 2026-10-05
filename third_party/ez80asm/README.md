# ez80asm

`ez80asm.bin` is the Agon build of **ez80asm 2.3** by Jeroen Venema, the
assembler that turns the compiler's linked `.asm` output into a program. It
is redistributed unmodified under the MIT licence in [LICENSE](LICENSE).

- Source and releases: https://github.com/AgonPlatform/agon-ez80asm (tag v2.3)
- On the SD card it goes to `/bin/ez80asm.bin`; `make sdcard` and
  `make cross` put it there.

The compiler's host build (`make host`, `make cross`) also needs ez80asm
built for the host; see BUILDING.md.
