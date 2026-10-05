/* twice.c - calls twice(), written in assembly in twice.s (driver.md 2's
 * example); exits with 42 through the emulator. */

void agon_emu_exit(int status);
int twice(int n);

int main(void)
{
    agon_emu_exit(twice(21));
    return 0;
}
