/* hello.c - M3's end-to-end target (no preprocessor yet: prototypes by hand) */
int puts(char *s);
void agon_emu_exit(int status);

int main(void)
{
    puts("hello, world");
    agon_emu_exit(42);
    return 0;
}
