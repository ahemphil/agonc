/* t_mosx.c - the rest of <agon/mos.h> (mosapi.c and sysvar.c, in
 * libagon.s) on the emulator: MOS's file calls beyond those libc uses,
 * the FatFS calls, the structures' sizes, the system variables, the UART
 * by MOS's names. Fixture: existing.txt ("Hello, Agon!\n", 13 bytes).
 *
 * The emulator's SD card is its own reimplementation of MOS's file layer
 * (mos.h), so these check what real MOS shares with it; the I2C bus and
 * the line editor are only called, not checked.
 */

#include <string.h>
#include "check.h"

char buf[300];
char data[16];

void test_mos_files(void)
{
    int h;
    int i;

    for (i = 0; i < 10; i++)
        data[i] = 'a' + i;
    check(mos_save("saved.bin", data, 10), 0);
    check((int)mos_fsize("saved.bin"), 10);
    memset(buf, 0, 16);
    check(mos_load("saved.bin", buf, 16), 0);
    check_mem(buf, "abcdefghij", 10);
    check(mos_copy("existing.txt", "copy.txt"), 0);
    check((int)mos_fsize("copy.txt"), 13);

    h = mos_fopen("existing.txt", FA_READ);
    check(mos_fgetc(h), 'H');
    check(mos_fgetc(h), 'e');
    check(mos_getfil(h) != 0, 1);
    mos_fclose(h);
    h = mos_fopen("bytes.txt", FA_WRITE | FA_CREATE_ALWAYS);
    mos_fputc(h, 'x');
    mos_fputc(h, 0xE9);
    mos_fclose(h);
    h = mos_fopen("bytes.txt", FA_READ);
    check(mos_fgetc(h), 'x');
    check(mos_fgetc(h), 0xE9);
    mos_fclose(h);

    buf[0] = 0;
    mos_getError(4, buf, 64);           /* FR_NO_FILE's message */
    check(buf[0] != 0, 1);

    check(mos_mkdir("sub"), 0);
    check(mos_cd("sub"), 0);
    check(ffs_getcwd(buf, 64), FR_OK);
    check_str(buf, "/sub");
    check(mos_cd("/"), 0);
    check(ffs_getcwd(buf, 64), FR_OK);
    check_str(buf, "/");
    check(mos_dir("/"), 0);             /* a listing on the screen */
}

FIL fil;
DIR dir;
FILINFO info;

void test_fatfs(void)
{
    unsigned int n;
    int found;
    int count;

    check((int)sizeof(FILINFO), 278);   /* as MOS 2.3.3's FatFS is built */
    check((int)sizeof(FIL), 36);
    check((int)sizeof(DIR), 49);

    check(ffs_fopen(&fil, "existing.txt", FA_READ), FR_OK);
    check(ffs_fread(&fil, buf, 5, &n), FR_OK);
    check((int)n, 5);
    check_mem(buf, "Hello", 5);
    check(ffs_feof(&fil), 0);
    check(ffs_flseek(&fil, 7L), FR_OK);
    check(ffs_fread(&fil, buf, 5, &n), FR_OK);
    check_mem(buf, "Agon!", 5);
    check(ffs_fread(&fil, buf, 50, &n), FR_OK);
    check((int)n, 1);                   /* the newline, and then the end */
    check(ffs_feof(&fil), 1);
    check(ffs_fclose(&fil), FR_OK);

    check(ffs_fopen(&fil, "ffs.txt", FA_WRITE | FA_CREATE_ALWAYS), FR_OK);
    check(ffs_fwrite(&fil, "123456", 6, &n), FR_OK);
    check((int)n, 6);
    check(ffs_flseek(&fil, 3L), FR_OK);
    check(ffs_ftruncate(&fil), FR_OK);
    check(ffs_fclose(&fil), FR_OK);
    check((int)mos_fsize("ffs.txt"), 3);
    check(ffs_fopen(&fil, "missing.txt", FA_READ), FR_NO_FILE);

    check(ffs_stat("existing.txt", &info), FR_OK);
    check((int)info.fsize, 13);
    check(info.fattrib & AM_DIR, 0);
    check_str(info.fname, "existing.txt");
    check(ffs_stat("sub", &info), FR_OK);
    check((info.fattrib & AM_DIR) != 0, 1);
    check(ffs_stat("missing.txt", &info), FR_NO_FILE);

    check(ffs_dopen(&dir, "/"), FR_OK);
    found = 0;
    count = 0;
    while (ffs_dread(&dir, &info) == FR_OK && info.fname[0] != 0 && count < 100) {
        count++;
        if (strcmp(info.fname, "existing.txt") == 0 && info.fsize == 13)
            found = 1;
    }
    check(found, 1);
    check(count >= 5, 1);               /* the fixture and the files made above */
    check(ffs_dclose(&dir), FR_OK);
}

void test_sysvars(void)
{
    volatile struct mos_sysvar *v;

    v = MOS_SYSVAR;
    check((int)((char *)&v->gp - mos_sysvars()), 0x37);
    check((int)((char *)&v->scr_cols - mos_sysvars()), 0x13);
    check((int)((char *)&v->mouse_x - mos_sysvars()), 0x29);
    check(getsysvar_scrCols(), v->scr_cols);
    check(getsysvar_scrRows(), v->scr_rows);
    check(getsysvar_cursorX(), v->cursor_x);
    check((int)getsysvar_scrwidth(), (int)v->scr_width);
    check(getsysvar_rtc() == v->rtc, 1);
    check(getsysvar_time() >= 0, 1);
}

struct mos_uart settings;

void test_uart_i2c(void)
{
    settings.baud[0] = 9600 & 255;
    settings.baud[1] = 9600 >> 8;
    settings.baud[2] = 0;
    settings.data_bits = 8;
    settings.stop_bits = 1;
    settings.parity = 0;
    settings.flow = 0;
    settings.interrupts = 0;
    check(mos_uopen(&settings), 0);
    check(mos_uputc('A'), 1);
    mos_uclose();
    check(mos_uputc('B'), 0);           /* closed */
    check(mos_getkbmap() != 0, 1);
    mos_i2c_open(1);                    /* no device here: only called */
    mos_i2c_close();
}

int main(void)
{
    test_mos_files();
    test_fatfs();
    test_sysvars();
    test_uart_i2c();
    return finish();
}
