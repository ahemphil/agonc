/* t_vdu.c - <agon/vdp.h>'s main, system, bitmap and sprite commands
 * (vdp.c, vdpsys.c, vdpbmp.c) send the bytes the VDP documentation gives.
 * The units are compiled into this program with AGON_VDU_CAPTURE defined,
 * so these vdu and vdu_n record the bytes instead of sending them; each
 * check is one command against its expected bytes. The stand-in vdu_n
 * also sets MOS's VDP reply flags, as the VDP's answer would, so the
 * requests do not wait. t_vdu2.c has the other groups.
 */

#include <stdarg.h>
#include <agon/vdp.h>
#include <agon/mos.h>
#include "check.h"

char got[400];
int ngot;

void vdu(int c)
{
    got[ngot++] = c;
}

void vdu_n(const char *p, int n)
{
    int i;

    for (i = 0; i < n; i++)
        got[ngot++] = p[i];
    MOS_SYSVAR->vdp_pflags = 0x7F;
}

/* The bytes sent since the last want must be exactly these n. */
void want(int n, ...)
{
    va_list ap;
    int i;
    int ok;

    ok = ngot == n;
    va_start(ap, n);
    for (i = 0; i < n; i++)
        if ((got[i] & 255) != (va_arg(ap, int) & 255))
            ok = 0;
    va_end(ap);
    check(ok, 1);
    ngot = 0;
}

unsigned char rows[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
unsigned long pixels[2] = { 0x44332211L, 0x88776655L };

void test_main(void)
{
    vdp_mode(3); want(2, 22, 3);
    vdp_cls(); want(1, 12);
    vdp_clg(); want(1, 16);
    vdp_colour(129); want(2, 17, 129);
    vdp_gcol(1, 5); want(3, 18, 1, 5);
    vdp_tab(10, 20); want(3, 31, 10, 20);
    vdp_cursor(7); want(3, 23, 1, 1);
    vdp_plot(69, 0x1234, 0x0567); want(6, 25, 69, 0x34, 0x12, 0x67, 0x05);
    vdp_move(1, 2); want(6, 25, 4, 1, 0, 2, 0);
    vdp_draw(-1, 2); want(6, 25, 5, 0xFF, 0xFF, 2, 0);
    vdp_send_to_printer('A'); want(2, 1, 'A');
    vdp_enable_printer(); want(1, 2);
    vdp_disable_printer(); want(1, 3);
    vdp_write_at_text_cursor(); want(1, 4);
    vdp_write_at_graphics_cursor(); want(1, 5);
    vdp_enable_screen(); want(1, 6);
    vdp_bell(); want(1, 7);
    vdp_cursor_left(); want(1, 8);
    vdp_cursor_right(); want(1, 9);
    vdp_cursor_down(); want(1, 10);
    vdp_cursor_up(); want(1, 11);
    vdp_clear_screen(); want(1, 12);
    vdp_carriage_return(); want(1, 13);
    vdp_page_mode_on(); want(1, 14);
    vdp_page_mode_off(); want(1, 15);
    vdp_clear_graphics(); want(1, 16);
    vdp_set_text_colour(2); want(2, 17, 2);
    vdp_set_graphics_colour(0, 3); want(3, 18, 0, 3);
    vdp_define_colour(1, 255, 10, 20, 30); want(6, 19, 1, 255, 10, 20, 30);
    vdp_reset_graphics(); want(1, 20);
    vdp_disable_screen(); want(1, 21);
    vdp_redefine_character(65, 1, 2, 3, 4, 5, 6, 7, 8); want(10, 23, 65, 1, 2, 3, 4, 5, 6, 7, 8);
    vdp_cursor_enable(0); want(3, 23, 1, 0);
    vdp_cursor_control(3); want(3, 23, 1, 3);
    vdp_set_dotted_line_pattern(0xAA, 0x55, 1, 2, 3, 4, 5, 6); want(10, 23, 6, 0xAA, 0x55, 1, 2, 3, 4, 5, 6);
    vdp_scroll_screen_extent(2, 3, 8); want(5, 23, 7, 2, 3, 8);
    vdp_scroll_screen(1, 0); want(5, 23, 7, 1, 1, 0);
    vdp_cursor_behaviour(1, 254); want(4, 23, 16, 1, 254);
    vdp_set_line_thickness(3); want(3, 23, 23, 3);
    vdp_set_graphics_viewport(1, 2, 0x300, 0x400); want(9, 24, 1, 0, 2, 0, 0, 3, 0, 4);
    vdp_move_to(5, 6); want(6, 25, 4, 5, 0, 6, 0);
    vdp_line_to(5, 6); want(6, 25, 5, 5, 0, 6, 0);
    vdp_point(5, 6); want(6, 25, 69, 5, 0, 6, 0);
    vdp_triangle(5, 6); want(6, 25, 85, 5, 0, 6, 0);
    vdp_circle_radius(5, 6); want(6, 25, 145, 5, 0, 6, 0);
    vdp_circle(5, 6); want(6, 25, 149, 5, 0, 6, 0);
    vdp_filled_rect(5, 6); want(6, 25, 101, 5, 0, 6, 0);
    vdp_reset_viewports(); want(1, 26);
    vdp_literal(7); want(2, 27, 7);
    vdp_set_text_viewport(1, 20, 30, 2); want(5, 28, 1, 20, 30, 2);
    vdp_graphics_origin(640, 512); want(5, 29, 0x80, 2, 0, 2);
    vdp_cursor_home(); want(1, 30);
    vdp_cursor_tab(3, 4); want(3, 31, 3, 4);
    vdp_backspace(); want(1, 127);
}

void test_system(void)
{
    unsigned char cx;
    unsigned char cy;

    vdp_set_cursor_start_line(0x41); want(4, 23, 0, 0x0A, 0x41);
    vdp_set_cursor_end_line(7); want(4, 23, 0, 0x0B, 7);
    vdp_set_cursor_start_column(1); want(4, 23, 0, 0x8A, 1);
    vdp_set_cursor_end_column(6); want(4, 23, 0, 0x8B, 6);
    vdp_move_cursor_relative(-2, 3); want(7, 23, 0, 0x8C, 0xFE, 0xFF, 3, 0);
    vdp_general_poll(9); want(4, 23, 0, 0x80, 9);
    vdp_request_text_cursor_position(1); want(3, 23, 0, 0x82);
    MOS_SYSVAR->cursor_x = 12;
    MOS_SYSVAR->cursor_y = 34;
    check(vdp_return_text_cursor_position(&cx, &cy), 0);
    check(cx * 100 + cy, 1234);
    want(3, 23, 0, 0x82);
    vdp_request_ascii_code_at_position(2, 3, 0); want(7, 23, 0, 0x83, 2, 0, 3, 0);
    MOS_SYSVAR->scrchar = 'Q';
    check(vdp_return_ascii_code_at_position(2, 3), 'Q'); want(7, 23, 0, 0x83, 2, 0, 3, 0);
    vdp_request_ascii_code_at_graphics_position(0x100, 4, 0); want(7, 23, 0, 0x93, 0, 1, 4, 0);
    check(vdp_return_ascii_code_at_graphics_position(1, 1), 'Q'); want(7, 23, 0, 0x93, 1, 0, 1, 0);
    vdp_request_pixel_colour(10, 11, 0); want(7, 23, 0, 0x84, 10, 0, 11, 0);
    MOS_SYSVAR->scrpixel[0] = 0x11;     /* red */
    MOS_SYSVAR->scrpixel[1] = 0x33;     /* blue */
    MOS_SYSVAR->scrpixel[2] = 0x22;     /* green */
    MOS_SYSVAR->scrpixel_index = 42;
    check(vdp_return_pixel_colour(10, 11) == 0x112233L, 1); want(7, 23, 0, 0x84, 10, 0, 11, 0);
    vdp_request_palette_entry(128, 0); want(4, 23, 0, 0x94, 128);
    check(vdp_return_palette_entry_colour(5) == 0x112233L, 1); want(4, 23, 0, 0x94, 5);
    check(vdp_return_palette_entry_index(5), 42); want(4, 23, 0, 0x94, 5);
    vdp_get_scr_dims(1); want(3, 23, 0, 0x86);
    vdp_request_rtc(1); want(4, 23, 0, 0x87, 0);
    vdp_set_rtc(46, 10, 3, 12, 30, 15); want(10, 23, 0, 0x87, 1, 46, 10, 3, 12, 30, 15);
    vdp_set_keyboard_locale(1); want(4, 23, 0, 0x81, 1);
    vdp_keyboard_control(500, 33, 2); want(8, 23, 0, 0x88, 0xF4, 1, 33, 0, 2);
    vdp_keyboard_cotrol(250, 100, 0); want(8, 23, 0, 0x88, 250, 0, 100, 0, 0);
    vdp_control_keys(5); want(4, 23, 0, 0x98, 1);
    vdp_request_key_state(0x5A); want(4, 23, 0, 0x99, 0x5A);
    vdp_mouse_enable(); want(4, 23, 0, 0x89, 0);
    vdp_mouse_disable(); want(4, 23, 0, 0x89, 1);
    vdp_mouse_reset(); want(4, 23, 0, 0x89, 2);
    vdp_mouse_set_cursor(65535); want(6, 23, 0, 0x89, 3, 0xFF, 0xFF);
    vdp_mouse_set_position(300, 2); want(8, 23, 0, 0x89, 4, 0x2C, 1, 2, 0);
    vdp_mouse_sample_rate(100); want(5, 23, 0, 0x89, 6, 100);
    vdp_mouse_resolution(3); want(5, 23, 0, 0x89, 7, 3);
    vdp_mouse_scaling(2); want(5, 23, 0, 0x89, 8, 2);
    vdp_mouse_acceleration(2000); want(6, 23, 0, 0x89, 9, 0xD0, 7);
    vdp_mouse_wheel_accel(100000L); want(7, 23, 0, 0x89, 10, 0xA0, 0x86, 1);
    vdp_redefine_character_special(1, 1, 2, 3, 4, 5, 6, 7, 8); want(12, 23, 0, 0x90, 1, 1, 2, 3, 4, 5, 6, 7, 8);
    vdp_define_character(200, rows); want(12, 23, 0, 0x90, 200, 1, 2, 3, 4, 5, 6, 7, 8);
    vdp_reset_system_font(); want(3, 23, 0, 0x91);
    vdp_map_char_to_bitmap(65, 64001); want(6, 23, 0, 0x92, 65, 0x01, 0xFA);
    vdp_set_text_viewport_via_plot(); want(3, 23, 0, 0x9C);
    vdp_set_graphics_viewport_via_plot(); want(3, 23, 0, 0x9D);
    vdp_set_graphics_origin_via_plot(); want(3, 23, 0, 0x9E);
    vdp_move_graphics_origin_and_viewport(); want(3, 23, 0, 0x9F);
    vdp_set_affine_transform(1, 7); want(6, 23, 0, 0x96, 1, 7, 0);
    vdp_page_mode_once(); want(3, 23, 0, 0x9A);
    vdp_print_buffer(0x1234); want(5, 23, 0, 0x9B, 0x34, 0x12);
    vdp_logical_scr_dims(0); want(4, 23, 0, 0xC0, 0);
    vdp_legacy_modes(1); want(4, 23, 0, 0xC1, 1);
    vdp_swap(); want(3, 23, 0, 0xC3);
    vdp_flush_drawing_commands(); want(3, 23, 0, 0xCA);
    vdp_set_dash_pattern_length(16); want(4, 23, 0, 0xF2, 16);
    vdp_set_variable(0x310, 1); want(7, 23, 0, 0xF8, 0x10, 3, 1, 0);
    vdp_clear_variable(2); want(5, 23, 0, 0xF9, 2, 0);
    vdp_console_mode(1); want(4, 23, 0, 0xFE, 1);
    vdp_terminal_mode(); want(3, 23, 0, 0xFF);
}

void test_bitmaps(void)
{
    vdp_select_bitmap(3); want(4, 23, 27, 0, 3);
    vdp_load_bitmap(2, 1, pixels); want(15, 23, 27, 1, 2, 0, 1, 0, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88);
    vdp_capture_bitmap(9); want(7, 23, 27, 1, 9, 0, 0, 0);
    vdp_solid_bitmap(16, 8, 255, 128, 0, 255); want(11, 23, 27, 2, 16, 0, 8, 0, 255, 128, 0, 255);
    vdp_draw_bitmap(320, 200); want(7, 23, 27, 3, 0x40, 1, 200, 0);
    vdp_adv_select_bitmap(64005); want(5, 23, 27, 0x20, 0x05, 0xFA);
    vdp_adv_bitmap_from_buffer(8, 4, 1); want(8, 23, 27, 0x21, 8, 0, 4, 0, 1);
    vdp_adv_capture_bitmap(300); want(7, 23, 27, 0x21, 0x2C, 1, 0, 0);
    vdp_select_sprite(2); want(4, 23, 27, 4, 2);
    vdp_clear_sprite(); want(3, 23, 27, 5);
    vdp_add_sprite_bitmap(7); want(4, 23, 27, 6, 7);
    vdp_activate_sprites(3); want(4, 23, 27, 7, 3);
    vdp_next_sprite_frame(); want(3, 23, 27, 8);
    vdp_prev_sprite_frame(); want(3, 23, 27, 9);
    vdp_nth_sprite_frame(1); want(4, 23, 27, 10, 1);
    vdp_show_sprite(); want(3, 23, 27, 11);
    vdp_hide_sprite(); want(3, 23, 27, 12);
    vdp_move_sprite_to(100, 50); want(7, 23, 27, 13, 100, 0, 50, 0);
    vdp_move_sprite_by(-1, 1); want(7, 23, 27, 14, 0xFF, 0xFF, 1, 0);
    vdp_refresh_sprites(); want(3, 23, 27, 15);
    vdp_reset_sprites(); want(3, 23, 27, 16);
    vdp_reset_sprites_only(); want(3, 23, 27, 17);
    vdp_set_sprite_paint_mode(3); want(4, 23, 27, 18, 3);
    vdp_set_sprite_hardware(); want(3, 23, 27, 19);
    vdp_set_sprite_software(); want(3, 23, 27, 20);
    vdp_replace_sprite_frame(5); want(5, 23, 27, 21, 5, 0);
    vdp_adv_add_sprite_bitmap(64010); want(5, 23, 27, 0x26, 0x0A, 0xFA);
    vdp_adv_replace_sprite_frame(64011); want(5, 23, 27, 0x35, 0x0B, 0xFA);
    vdp_create_sprite(1, 4, 2); want(15, 23, 27, 4, 1, 23, 27, 5, 23, 27, 6, 4, 23, 27, 6, 5);
    vdp_adv_create_sprite(0, 64000, 1); want(12, 23, 27, 4, 0, 23, 27, 5, 23, 27, 0x26, 0x00, 0xFA);
    vdp_mouse_cursor_from_bitmap(2, 3); want(5, 23, 27, 0x40, 2, 3);
    vdu(7); want(1, 7);                 /* (the stand-ins themselves) */
}

int main(void)
{
    test_main();
    test_system();
    test_bitmaps();
    return finish();
}
