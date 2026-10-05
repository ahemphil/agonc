# Examples

Short programs to compile with agonc and read. Each opens with a comment
saying what it shows and how to build it. Copy them to the Agon's SD card,
then, for example:

```text
agonc -o numbers.bin numbers.c
numbers
```

| Program | Shows |
|---|---|
| [hello.c](hello.c) | the smallest complete program |
| [files.c](files.c) | writing, reading, appending, renaming and removing a file with the standard library, and checking for errors |
| [numbers.c](numbers.c) | `double` arithmetic (a Mandelbrot set in text, and some well-known IEEE results) and `long long` (factorials), done in software and exactly as on a PC |
| [graphics.c](graphics.c) | a screen mode, colours, lines, filled shapes and placed text through `<agon/vdp.h>` |
| [sound.c](sound.c) | notes, waveforms, a chord and a volume envelope through the VDP's sound system |
| [serial.c](serial.c) | an echo session with another machine over the second serial port, UART1, through `<agon/uart.h>` |

The test suite compiles every example, and runs hello, files and numbers on
the emulator, checking that each prints exactly what the same program
prints when built for a PC. The [manual](../docs/manual/README.md) covers the
libraries they use.
