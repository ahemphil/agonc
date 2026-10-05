/* t_vdu2.c - <agon/vdp.h>'s audio, buffer, context, font, copper and
 * tile commands (vdpaudio.c, vdpbuf.c, vdpmore.c) send the bytes the VDP
 * documentation gives; recorded as in t_vdu.c, one check a command.
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

unsigned char wave[3] = { 0x10, 0x20, 0x30 };
unsigned char phases[5] = { 1, 127, 10, 0, 0 };
int steps[4] = { 40, 6, -30, 4 };
int ids[2] = { 5, 6 };
unsigned char tilepix[64];
unsigned char args[3] = { 9, 8, 7 };

void test_audio(void)
{
    vdp_audio_play_note(1, 60, 440, 1500); want(10, 23, 0, 0x85, 1, 0, 60, 0xB8, 1, 0xDC, 5);
    MOS_SYSVAR->audio_success = 3;
    check(vdp_audio_status(2), 3); want(5, 23, 0, 0x85, 2, 1);
    vdp_audio_set_volume(255, 100); want(6, 23, 0, 0x85, 255, 2, 100);
    vdp_audio_set_frequency(0, 523); want(7, 23, 0, 0x85, 0, 3, 0x0B, 2);
    vdp_audio_set_waveform(1, -1); want(6, 23, 0, 0x85, 1, 4, 0xFF);
    vdp_audio_set_sample(1, 64256); want(8, 23, 0, 0x85, 1, 4, 8, 0x00, 0xFB);
    vdp_audio_load_sample(-1, 3L, wave); want(12, 23, 0, 0x85, 0xFF, 5, 0, 3, 0, 0, 0x10, 0x20, 0x30);
    vdp_audio_clear_sample(-2); want(6, 23, 0, 0x85, 0xFE, 5, 1);
    vdp_audio_create_sample_from_buffer(0, 300, 16); want(9, 23, 0, 0x85, 0, 5, 2, 0x2C, 1, 16);
    vdp_audio_create_sample_from_buffer_rate(0, 300, 1, 22050); want(11, 23, 0, 0x85, 0, 5, 2, 0x2C, 1, 9, 0x22, 0x56);
    vdp_audio_set_sample_frequency(-1, 523); want(8, 23, 0, 0x85, 0xFF, 5, 3, 0x0B, 2);
    vdp_audio_set_buffer_frequency(0, 300, 440); want(10, 23, 0, 0x85, 0, 5, 4, 0x2C, 1, 0xB8, 1);
    vdp_audio_set_sample_repeat_start(-1, 0x12345L); want(9, 23, 0, 0x85, 0xFF, 5, 5, 0x45, 0x23, 1);
    vdp_audio_set_buffer_repeat_start(0, 300, 100L); want(11, 23, 0, 0x85, 0, 5, 6, 0x2C, 1, 100, 0, 0);
    vdp_audio_set_sample_repeat_length(-1, -1L); want(9, 23, 0, 0x85, 0xFF, 5, 7, 0xFF, 0xFF, 0xFF);
    vdp_audio_set_buffer_repeat_length(0, 300, 7L); want(11, 23, 0, 0x85, 0, 5, 8, 0x2C, 1, 7, 0, 0);
    vdp_audio_volume_envelope_disable(1); want(6, 23, 0, 0x85, 1, 6, 0);
    vdp_audio_volume_envelope_ADSR(1, 400, 100, 100, 2000); want(13, 23, 0, 0x85, 1, 6, 1, 0x90, 1, 100, 0, 100, 0xD0, 7);
    vdp_audio_volume_envelope_multiphase_ADSR(1); want(6, 23, 0, 0x85, 1, 6, 2);
    vdp_audio_volume_envelope_multiphase(1, phases, 5); want(11, 23, 0, 0x85, 1, 6, 2, 1, 127, 10, 0, 0);
    vdp_audio_frequency_envelope_disable(1); want(6, 23, 0, 0x85, 1, 7, 0);
    vdp_audio_frequency_envelope_stepped(1, 2, 1, 30); want(10, 23, 0, 0x85, 1, 7, 1, 2, 1, 30, 0);
    vdp_audio_frequency_envelope(1, 3, 30, steps, 2);
    want(18, 23, 0, 0x85, 1, 7, 1, 2, 3, 30, 0, 40, 0, 6, 0, 0xE2, 0xFF, 4, 0);
    vdp_audio_enable_channel(5); want(5, 23, 0, 0x85, 5, 8);
    vdp_audio_disable_channel(5); want(5, 23, 0, 0x85, 5, 9);
    vdp_audio_reset_channel(5); want(5, 23, 0, 0x85, 5, 10);
    vdp_audio_sample_seek(1, 0x10000L); want(8, 23, 0, 0x85, 1, 11, 0, 0, 1);
    vdp_audio_sample_duration(1, 70000L); want(8, 23, 0, 0x85, 1, 12, 0x70, 0x11, 1);
    vdp_audio_sample_rate(255, 32768); want(7, 23, 0, 0x85, 255, 13, 0, 0x80);
    vdp_audio_set_waveform_parameter(0, 0, 128); want(7, 23, 0, 0x85, 0, 14, 0, 128);
    vdp_audio_set_waveform_parameter(0, 0x83, 1000); want(8, 23, 0, 0x85, 0, 14, 0x83, 0xE8, 3);
}

void test_buffers(void)
{
    vdp_adv_command(7, 32, args, 3); want(9, 23, 0, 0xA0, 7, 0, 32, 9, 8, 7);
    vdp_adv_write_block(7, 3); want(8, 23, 0, 0xA0, 7, 0, 0, 3, 0);
    vdp_adv_write_block_data(7, 2, "AB"); want(10, 23, 0, 0xA0, 7, 0, 0, 2, 0, 'A', 'B');
    vdp_adv_call_buffer(0x1234); want(6, 23, 0, 0xA0, 0x34, 0x12, 1);
    vdp_adv_clear_buffer(65535); want(6, 23, 0, 0xA0, 0xFF, 0xFF, 2);
    vdp_adv_create(7, 256); want(8, 23, 0, 0xA0, 7, 0, 3, 0, 1);
    vdp_adv_stream(7); want(6, 23, 0, 0xA0, 7, 0, 4);
    vdp_adv_adjust(3, 3, 12); want(9, 23, 0, 0xA0, 3, 0, 5, 3, 12, 0);
    vdp_adv_call_conditional(7, 0, 12, 5); want(11, 23, 0, 0xA0, 7, 0, 6, 0, 12, 0, 5, 0);
    vdp_adv_jump_buffer(7); want(6, 23, 0, 0xA0, 7, 0, 7);
    vdp_adv_jump_conditional(7, 2, 12, 5); want(11, 23, 0, 0xA0, 7, 0, 8, 2, 12, 0, 5, 0);
    vdp_adv_jump_offset(7, 0x10203L); want(9, 23, 0, 0xA0, 7, 0, 9, 3, 2, 1);
    vdp_adv_jump_offset_block(7, 4L, 2); want(11, 23, 0, 0xA0, 7, 0, 9, 4, 0, 0x80, 2, 0);
    vdp_adv_jump_offset_conditional(7, 4L); want(9, 23, 0, 0xA0, 7, 0, 10, 4, 0, 0);
    vdp_adv_jump_offset_block_conditional(7, 4L, 1); want(11, 23, 0, 0xA0, 7, 0, 10, 4, 0, 0x80, 1, 0);
    vdp_adv_call_offset(7, 4L); want(9, 23, 0, 0xA0, 7, 0, 11, 4, 0, 0);
    vdp_adv_call_offset_block(7, 4L, 1); want(11, 23, 0, 0xA0, 7, 0, 11, 4, 0, 0x80, 1, 0);
    vdp_adv_call_offset_conditional(7, 4L); want(9, 23, 0, 0xA0, 7, 0, 12, 4, 0, 0);
    vdp_adv_call_offset_block_conditional(7, 4L, 1); want(11, 23, 0, 0xA0, 7, 0, 12, 4, 0, 0x80, 1, 0);
    vdp_adv_copy_multiple(9, 2, 5, 6); want(12, 23, 0, 0xA0, 9, 0, 13, 5, 0, 6, 0, 0xFF, 0xFF);
    vdp_adv_copy_blocks(9, ids, 2); want(12, 23, 0, 0xA0, 9, 0, 13, 5, 0, 6, 0, 0xFF, 0xFF);
    vdp_adv_consolidate(9); want(6, 23, 0, 0xA0, 9, 0, 14);
    vdp_adv_split(9, 64); want(8, 23, 0, 0xA0, 9, 0, 15, 64, 0);
    vdp_adv_split_multiple(9, 64, 1, 5); want(12, 23, 0, 0xA0, 9, 0, 16, 64, 0, 5, 0, 0xFF, 0xFF);
    vdp_adv_split_multiple_from(9, 64, 100); want(10, 23, 0, 0xA0, 9, 0, 17, 64, 0, 100, 0);
    vdp_adv_split_by_width(9, 16, 4); want(10, 23, 0, 0xA0, 9, 0, 18, 16, 0, 4, 0);
    vdp_adv_split_by_width_multiple(9, 16, 2, 5, 6); want(14, 23, 0, 0xA0, 9, 0, 19, 16, 0, 5, 0, 6, 0, 0xFF, 0xFF);
    vdp_adv_split_by_width_multiple_from(9, 16, 4, 100); want(12, 23, 0, 0xA0, 9, 0, 20, 16, 0, 4, 0, 100, 0);
    vdp_adv_spread_multiple(9, 1, 5); want(10, 23, 0, 0xA0, 9, 0, 21, 5, 0, 0xFF, 0xFF);
    vdp_adv_spread_multiple_from(9, 100); want(8, 23, 0, 0xA0, 9, 0, 22, 100, 0);
    vdp_adv_reverse_block_order(9); want(6, 23, 0, 0xA0, 9, 0, 23);
    vdp_adv_reverse_block_data(9, 2, 0, 0); want(7, 23, 0, 0xA0, 9, 0, 24, 2);
    vdp_adv_reverse_block_data(9, 3, 320, 0); want(9, 23, 0, 0xA0, 9, 0, 24, 3, 0x40, 1);
    vdp_adv_reverse_block_data(9, 7, 320, 1280); want(11, 23, 0, 0xA0, 9, 0, 24, 7, 0x40, 1, 0, 5);
    vdp_adv_copy_multiple_by_reference(9, 1, 5); want(10, 23, 0, 0xA0, 9, 0, 25, 5, 0, 0xFF, 0xFF);
    vdp_adv_copy_multiple_consolidate(9, 1, 5); want(10, 23, 0, 0xA0, 9, 0, 26, 5, 0, 0xFF, 0xFF);
    vdp_adv_compress_buffer(9, 5); want(8, 23, 0, 0xA0, 9, 0, 64, 5, 0);
    vdp_adv_decompress_buffer(9, 5); want(8, 23, 0, 0xA0, 9, 0, 65, 5, 0);
    vdp_adv_set_callback(9, 0); want(8, 23, 0, 0xA0, 9, 0, 80, 0, 0);
    vdp_adv_remove_callback(65535, 65535); want(8, 23, 0, 0xA0, 0xFF, 0xFF, 81, 0xFF, 0xFF);
    vdp_adv_debug_info(9); want(6, 23, 0, 0xA0, 9, 0, 128);
}

void test_more(void)
{
    vdp_context_select(1); want(5, 23, 0, 0xC8, 0, 1);
    vdp_context_delete(1); want(5, 23, 0, 0xC8, 1, 1);
    vdp_context_reset(255); want(5, 23, 0, 0xC8, 2, 255);
    vdp_context_save(); want(4, 23, 0, 0xC8, 3);
    vdp_context_restore(); want(4, 23, 0, 0xC8, 4);
    vdp_context_save_copy(2); want(5, 23, 0, 0xC8, 5, 2);
    vdp_context_restore_all(); want(4, 23, 0, 0xC8, 6);
    vdp_context_clear_stack(); want(4, 23, 0, 0xC8, 7);
    vdp_font_select(65535, 0); want(7, 23, 0, 0x95, 0, 0xFF, 0xFF, 0);
    vdp_font_create(300, 8, 16, 12, 0); want(10, 23, 0, 0x95, 1, 0x2C, 1, 8, 16, 12, 0);
    vdp_font_adjust(300, 2, 1000); want(9, 23, 0, 0x95, 2, 0x2C, 1, 2, 0xE8, 3);
    vdp_font_delete(300); want(6, 23, 0, 0x95, 4, 0x2C, 1);
    vdp_font_copy(300); want(6, 23, 0, 0x95, 5, 0x2C, 1);
    vdp_copper_create_palette(10); want(6, 23, 0, 0xC4, 0, 10, 0);
    vdp_copper_delete_palette(65535); want(6, 23, 0, 0xC4, 1, 0xFF, 0xFF);
    vdp_copper_set_palette_entry(10, 3, 255, 128, 0); want(10, 23, 0, 0xC4, 2, 10, 0, 3, 255, 128, 0);
    vdp_copper_set_signal_list(20); want(6, 23, 0, 0xC4, 3, 20, 0);
    vdp_copper_reset_signal_list(); want(4, 23, 0, 0xC4, 4);
    vdp_tile_bank_init(0); want(8, 23, 0, 0xC2, 0, 0, 0, 0, 0);
    tilepix[0] = 0xC3;
    tilepix[63] = 0x3C;
    vdp_tile_load(0, 5, tilepix);
    check(ngot == 70 && (got[5] & 255) == 5 && (got[6] & 255) == 0xC3 && (got[69] & 255) == 0x3C, 1);
    ngot = 0;
    vdp_tile_draw(0, 5, 10, 20, 1, 2, 3); want(12, 23, 0, 0xC2, 6, 0, 5, 0, 10, 20, 1, 2, 3);
    vdp_tile_bank_free(1); want(5, 23, 0, 0xC2, 7, 1);
    vdp_tile_map_init(4); want(8, 23, 0, 0xC2, 16, 0, 4, 0, 0);
    vdp_tile_map_set(3, 4, 5, 6); want(9, 23, 0, 0xC2, 17, 0, 3, 4, 5, 6);
    vdp_tile_map_free(); want(5, 23, 0, 0xC2, 23, 0);
    vdp_tile_layer_init(2); want(8, 23, 0, 0xC2, 24, 0, 2, 0, 0);
    vdp_tile_layer_scroll(1, 2, 3, 4); want(9, 23, 0, 0xC2, 26, 0, 1, 2, 3, 4);
    vdp_tile_layer_update(); want(5, 23, 0, 0xC2, 28, 0);
    vdp_tile_layer_draw(); want(5, 23, 0, 0xC2, 29, 0);
    vdp_tile_layer_update_draw(); want(5, 23, 0, 0xC2, 30, 0);
}

int main(void)
{
    test_audio();
    test_buffers();
    test_more();
    return finish();
}
