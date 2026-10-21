/* t_mosx.c - the rest of <agon/mos.h> (mosapi.c and sysvar.c, in
 * libagon.s) on the emulator: MOS's file calls beyond those libc uses,
 * the FatFS calls, the structures' sizes, the system variables, the UART
 * by MOS's names. Fixture: existing.txt ("Hello, Agon!\n", 13 bytes).
 *
 * The emulator's SD card is its own reimplementation of MOS's file layer
 * (mos.h), so these check what real MOS shares with it; the I2C bus and
 * the line editor are only called, not checked.
 */

#include <stddef.h>
#include <string.h>
#include <agon/mos.h>
#include <agon/vdp.h>
#include <agon/gpio.h>
#include <agon/joystick.h>
#include <agon/keyboard.h>
#include <agon/timer.h>
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
    int found;
    int count;

    check((int)sizeof(FILINFO), 278);   /* as MOS 2.3.3's FatFS is built */
    check((int)sizeof(FIL), 36);
    check((int)sizeof(DIR), 49);

    check(ffs_fopen(&fil, "existing.txt", FA_READ), FR_OK);
    check((int)ffs_fread(&fil, buf, 5), 5);    /* AgonDev's form: the count */
    check_mem(buf, "Hello", 5);
    check(ffs_feof(&fil), 0);
    check(ffs_flseek(&fil, 7L), FR_OK);
    check((int)ffs_fread(&fil, buf, 5), 5);
    check_mem(buf, "Agon!", 5);
    check((int)ffs_fread(&fil, buf, 50), 1);   /* the newline, and then the end */
    check(ffs_feof(&fil), 1);
    check(ffs_fclose(&fil), FR_OK);

    check(ffs_fopen(&fil, "ffs.txt", FA_WRITE | FA_CREATE_ALWAYS), FR_OK);
    check((int)ffs_fwrite(&fil, "123456", 6), 6);
    check(ffs_flseek(&fil, 3L), FR_OK);
    check(ffs_ftruncate(&fil), FR_OK);
    check(ffs_fclose(&fil), FR_OK);
    check((int)mos_fsize("ffs.txt"), 3);
    check(ffs_fopen(&fil, "missing.txt", FA_READ), FR_NO_FILE);

    check(ffs_stat(&info, "existing.txt"), FR_OK);
    check((int)info.fsize, 13);
    check(info.fattrib & AM_DIR, 0);
    check_str(info.fname, "existing.txt");
    check(ffs_stat(&info, "sub"), FR_OK);
    check((info.fattrib & AM_DIR) != 0, 1);
    check(ffs_stat(&info, "missing.txt"), FR_NO_FILE);

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

/* AgonDev's names (v0.22): the sysvar_ offsets against both structures,
 * sys_vars, the mouse readers, waitvblank, mos_ugetc_nb, mos_oscli's three
 * arguments, FRESULT. (mos_puts and the other output calls are t_vdu's.) */
void test_agondev(void)
{
    volatile struct mos_sysvar *v;
    unsigned long t;
    FRESULT fr;
    int r;

    check((int)offsetof(struct mos_sysvar, cursor_x), sysvar_cursorX);
    check((int)offsetof(struct mos_sysvar, scr_width), sysvar_scrWidth);
    check((int)offsetof(struct mos_sysvar, scrpixel_index), sysvar_scrpixelIndex);
    check((int)offsetof(struct mos_sysvar, rtc), sysvar_rtc);
    check((int)offsetof(struct mos_sysvar, keydelay), sysvar_keydelay);
    check((int)offsetof(struct mos_sysvar, scr_mode), sysvar_scrMode);
    check((int)offsetof(struct mos_sysvar, mouse_x), sysvar_mouseX);
    check((int)offsetof(struct mos_sysvar, mouse_ydelta), sysvar_mouseYDelta);
    check((int)offsetof(struct mos_sysvar, gp), sysvar_gp);
    check((int)offsetof(SYSVAR, vdp_pflags), sysvar_vdp_pflags);
    check((int)offsetof(SYSVAR, scrpixel), sysvar_scrpixel);
    check((int)offsetof(SYSVAR, audioSuccess), syscar_audioSuccess);
    check((int)offsetof(SYSVAR, scrHeight), sysvar_scrHeight);
    check((int)offsetof(SYSVAR, vkeycount), sysvar_vkeycount);
    check((int)offsetof(SYSVAR, rtc), sysvar_rtc);
    check((int)offsetof(SYSVAR, keyled), sysvar_keyled);
    check((int)offsetof(SYSVAR, rtcEnable), sysvar_rtcEnable);
    check((int)offsetof(SYSVAR, mouseWheel), sysvar_mouseWheel);
    check((int)offsetof(SYSVAR, mouseYDelta), sysvar_mouseYDelta);
    check((int)sizeof(SYSVAR), 0x33);
    check((int)sizeof(SYSVAR_RTCDATA), 6);
    check(vdp_pflag_mode, VDP_PFLAG_MODE);
    check(FA_OPEN_EXISTING, 0);

    v = MOS_SYSVAR;
    check(sys_vars->scrCols, v->scr_cols);
    check((int)sys_vars->scrWidth, (int)v->scr_width);
    check(mos_sysvars()[sysvar_scrRows], v->scr_rows);
    check((int)getsysvar_mouseX(), (int)v->mouse_x);
    check(getsysvar_mouseButtons(), v->mouse_buttons);
    check(getsysvar_mouseWheel(), v->mouse_wheel);
    check(getsysvar_mouseXDelta(), v->mouse_xdelta);

    t = getsysvar_time();
    waitvblank();
    check(getsysvar_time() != t, 1);

    r = mos_ugetc_nb();
    check(r >= -1 && r <= 255, 1);
    check(mos_oscli("cd /", NULL, 0), 0);
    fr = ffs_stat(&info, "existing.txt");
    check(fr, FR_OK);
}

/* AgonDev's hardware headers: the ports read directly, the pins of port C
 * (the user port; port B carries the VDP's interrupt and port D UART0, so
 * neither is written), the joysticks, the delay against MOS's clock, and
 * the key-event queue set up, polled empty and taken down. */
void test_agondev_hw(void)
{
    struct keyboard_event_t e;
    unsigned long t;
    unsigned int b;
    int ddr;

    check(io_in(PC_DR) >= 0 && io_in(PC_DR) <= 255, 1);
    check(inputPort(PORTC), io_in(PC_DR));
    check(input(PORTC, 3), (io_in(PC_DR) & 8) != 0);
    check(inputPinID(PC3), input(PORTC, 3));
    check(input(PORTC, 8), 0);
    check(PB0, 0x9A01);
    check(PD7, 0xA280);

    ddr = io_in(PC_DDR);
    outputMode(PORTC, 0);
    check(io_in(PC_DDR) & 1, 0);
    inputModePinID(PC0);
    check(io_in(PC_DDR) & 1, 1);
    outputModePinID(PC0);
    check(io_in(PC_DDR) & 1, 0);
    inputMode(PORTC, 0);
    check(io_in(PC_DDR) & 1, 1);
    io_out(PC_DDR, ddr);
    check(io_in(PC_DDR), ddr);

    b = getJoystickButtons();
    check(b <= 0xFFFF, 1);
    check(joystick1_UP(b), !(b & 2));
    check(joystick2_BUTTON2(b), !(b & 0x4000));

    /* (the emulator runs unthrottled here, timer 0 at the emulated clock
     * and MOS's at the host's, so only that it ends is checked; at real
     * speed delay(100) took 12 centiseconds) */
    t = getsysvar_time();
    delay(50);
    check(getsysvar_time() - t < 100, 1);
    delay(0);

    kbuf_init(0);
    check(kbuf_poll_event(&e), 0);
    kbuf_init(8);
    check(kbuf_poll_event(&e), 0);
    kbuf_clear();
    check(kbuf_poll_event(&e), 0);
    kbuf_deinit();
    check(kbuf_poll_event(&e), 0);
}

int main(void)
{
    test_mos_files();
    test_fatfs();
    test_sysvars();
    test_uart_i2c();
    test_agondev();
    test_agondev_hw();
    return finish();
}
