/* agon/vdp.h - VDU commands to the Agon's VDP, every one its
 * documentation describes (agonplatform.github.io/agon-docs, the VDP
 * pages), as C functions. In libagon.s (vdp.c and the vdp*.c units); a
 * program links only the ones it calls.
 *
 * The bytes go straight to MOS's VDU output (RST 10h and 18h), past
 * stdio's buffers, which write the console at the end of every output
 * call, so the two mix in order. Coordinates are the VDP's graphics units
 * (1280 x 1024 with the origin at the bottom left, unless logical scaling
 * is off) except where a comment says pixels. Arguments are ints; where
 * the VDP takes a byte only the low 8 bits are sent, and where it takes
 * 16 bits, the low 16. Each call is an interruption point for Ctrl-C
 * (<signal.h>).
 *
 * The names follow AgDev's agon/vdp_vdu.h where it has the command, so
 * programs written for AgDev build unchanged; the rest follow the same
 * pattern. Commands newer than the original VDP note the Console8 VDP
 * release that added them: an older VDP ignores them. What a command does
 * in detail, and its options, are in the VDP documentation; the comments
 * here say what the arguments are. Not part of C89.
 *
 * Each function sends a VDU command (a few send several): its control
 * code (VDU 23 and a group number for most of the newer ones), then its
 * argument bytes, with 16-bit values low byte first. vdp.c's opening
 * comment describes the protocol. vdu and vdu_n send any bytes: a
 * command not covered here, or the variable-length part of one whose
 * function sends only its start.
 */

#ifndef _AGON_VDP_H
#define _AGON_VDP_H

/* Raw VDU bytes: one, or n from p (nothing if n <= 0). */
void vdu(int c);
void vdu_n(const char *p, int n);

/* ---- the main commands, VDU 0-31 and 127 (vdp.c) -------------------------------------- */

void vdp_mode(int mode);                /* VDU 22: a screen mode */
void vdp_cls(void);                     /* VDU 12: clear the text area */
void vdp_clg(void);                     /* VDU 16: clear the graphics area */
void vdp_colour(int c);                 /* VDU 17: text colour (128 + c: background) */
void vdp_gcol(int mode, int c);         /* VDU 18: graphics colour and paint mode */
void vdp_tab(int x, int y);             /* VDU 31: text cursor to column x, row y */
void vdp_cursor(int on);                /* VDU 23,1: show or hide the text cursor */
void vdp_plot(int k, int x, int y);     /* VDU 25: PLOT k, x, y (BBC BASIC's codes) */
void vdp_move(int x, int y);            /* PLOT 4 */
void vdp_draw(int x, int y);            /* PLOT 5: a line from the last point */

void vdp_send_to_printer(int c);        /* VDU 1: c to the "printer" (the USB serial port) */
void vdp_enable_printer(void);          /* VDU 2 */
void vdp_disable_printer(void);         /* VDU 3 */
void vdp_write_at_text_cursor(void);    /* VDU 4: text at the text cursor */
void vdp_write_at_graphics_cursor(void);  /* VDU 5: text at the graphics cursor */
void vdp_enable_screen(void);           /* VDU 6 */
void vdp_bell(void);                    /* VDU 7 */
void vdp_cursor_left(void);             /* VDU 8 */
void vdp_cursor_right(void);            /* VDU 9 */
void vdp_cursor_down(void);             /* VDU 10 */
void vdp_cursor_up(void);               /* VDU 11 */
void vdp_clear_screen(void);            /* VDU 12, as vdp_cls */
void vdp_carriage_return(void);         /* VDU 13 */
void vdp_page_mode_on(void);            /* VDU 14: stop at each screenful */
void vdp_page_mode_off(void);           /* VDU 15 */
void vdp_clear_graphics(void);          /* VDU 16, as vdp_clg */
void vdp_set_text_colour(int colour);   /* VDU 17, as vdp_colour */
void vdp_set_graphics_colour(int mode, int colour);   /* VDU 18, as vdp_gcol */
/* VDU 19: logical colour to physical colour, or to red, green, blue
 * (0-255) when physical is 255. */
void vdp_define_colour(int logical, int physical, int red, int green, int blue);
void vdp_reset_graphics(void);          /* VDU 20: the default colours and modes */
void vdp_disable_screen(void);          /* VDU 21 */

/* VDU 23, c: character c (32-255) as eight rows, top first. */
void vdp_redefine_character(int c, int b0, int b1, int b2, int b3, int b4, int b5, int b6, int b7);
void vdp_cursor_enable(int on);         /* VDU 23, 1, as vdp_cursor */
void vdp_cursor_control(int n);         /* VDU 23, 1: 0 hide, 1 show, 2 steady, 3 flashing */
/* VDU 23, 6: the dotted-line pattern, most significant bit first. */
void vdp_set_dotted_line_pattern(int b0, int b1, int b2, int b3, int b4, int b5, int b6, int b7);
/* VDU 23, 7: scroll. extent 0 the text viewport, 1 the screen, 2 the
 * graphics viewport, 3 the active viewport; direction 0 right, 1 left, 2
 * down, 3 up; speed in pixels (0: a character). */
void vdp_scroll_screen_extent(int extent, int direction, int speed);
void vdp_scroll_screen(int direction, int speed);   /* the whole screen */
void vdp_cursor_behaviour(int setting, int mask);   /* VDU 23, 16: (old AND mask) XOR setting */
void vdp_set_line_thickness(int pixels);  /* VDU 23, 23 (VDP 2.6.0) */

void vdp_set_graphics_viewport(int left, int bottom, int right, int top);  /* VDU 24 */
void vdp_move_to(int x, int y);         /* PLOT 4 */
void vdp_line_to(int x, int y);         /* PLOT 5 */
void vdp_point(int x, int y);           /* PLOT 69 */
/* The next three use PLOT codes that draw (85, 145, 149); AgDev's
 * versions send the matching "move" codes, which draw nothing. */
void vdp_triangle(int x, int y);        /* PLOT 85: filled, with the last two points */
void vdp_circle_radius(int x, int y);   /* PLOT 145: about the last point, radius x, y away */
void vdp_circle(int x, int y);          /* PLOT 149: about the last point, through x, y */
void vdp_filled_rect(int x, int y);     /* PLOT 101: from the last point */
void vdp_reset_viewports(void);         /* VDU 26 */
void vdp_literal(int c);                /* VDU 27: c as a character, even a control code */
void vdp_set_text_viewport(int left, int bottom, int right, int top);  /* VDU 28: columns and rows */
void vdp_graphics_origin(int x, int y);   /* VDU 29 */
void vdp_cursor_home(void);             /* VDU 30 */
void vdp_cursor_tab(int x, int y);      /* VDU 31, as vdp_tab */
void vdp_backspace(void);               /* VDU 127 */

/* ---- system commands, VDU 23, 0, n (vdpsys.c) --------------------------------------------- */

/* MOS's VDP reply flags (MOS_SYSVAR->vdp_pflags in agon/mos.h): a request
 * clears its bit and the VDP's answer sets it. The palette requests share
 * VDP_PFLAG_POINT with the pixel reads, and the audio status uses
 * VDP_PFLAG_AUDIO. */
#define VDP_PFLAG_CURSOR    0x01
#define VDP_PFLAG_SCRCHAR   0x02
#define VDP_PFLAG_POINT     0x04
#define VDP_PFLAG_AUDIO     0x08
#define VDP_PFLAG_MODE      0x10
#define VDP_PFLAG_RTC       0x20
#define VDP_PFLAG_MOUSE     0x40

/* The text cursor's shape: start and end rows (start's bits 5-6: 0 steady,
 * 1 off, 2 fast, 3 slow blink) and columns (VDP 2.7.0). */
void vdp_set_cursor_start_line(int n);
void vdp_set_cursor_end_line(int n);
void vdp_set_cursor_start_column(int n);
void vdp_set_cursor_end_column(int n);
void vdp_move_cursor_relative(int x, int y);   /* by pixels (VDP 2.8.0) */

/* Requests. The answer lands in the system variables (agon/mos.h). With
 * wait non-zero a request waits for it, at most a second; the "return"
 * functions request, wait, and give the answer, or -1 if none came. */
void vdp_general_poll(int n);           /* the VDP echoes n (MOS uses this at start-up) */
void vdp_request_text_cursor_position(int wait);
int vdp_return_text_cursor_position(unsigned char *x, unsigned char *y);  /* 0, or -1 */
void vdp_request_ascii_code_at_position(int x, int y, int wait);
int vdp_return_ascii_code_at_position(int x, int y);      /* the character, 0 if unknown */
void vdp_request_ascii_code_at_graphics_position(int x, int y, int wait);  /* (VDP 2.8.0) */
int vdp_return_ascii_code_at_graphics_position(int x, int y);
void vdp_request_pixel_colour(int x, int y, int wait);
long vdp_return_pixel_colour(int x, int y);               /* 0xRRGGBB */
/* Palette entry n: 0-63, or 128-131 for the current text foreground and
 * background and graphics foreground and background (VDP 2.4.0). */
void vdp_request_palette_entry(int n, int wait);
long vdp_return_palette_entry_colour(int n);              /* 0xRRGGBB */
int vdp_return_palette_entry_index(int n);                /* its colour number */
void vdp_get_scr_dims(int wait);        /* the screen's size and mode, into the sysvars */
void vdp_request_rtc(int wait);         /* the clock, into the sysvars */
/* Set the clock: the year less 1980, month, day, hour, minute, second. */
void vdp_set_rtc(int year, int month, int day, int hour, int minute, int second);

/* The keyboard: its locale (0 UK, 1 US, 2 German, 3 Italian, 4 Spanish, 5
 * French, 6 Belgian, 7 Norwegian, 8 Japanese, 9 US International, 10 US
 * International Alternate, 11 Swiss German, 12 Swiss French, 13 Danish,
 * 14 Swedish, 15 Portuguese, 16 Brazilian Portuguese, 17 Dvorak); its
 * repeat delay (250-1000) and rate (33-500) in ms and lights (bits 0
 * Scroll, 1 Caps, 2 Num Lock); whether Ctrl+letter keys act on the VDP
 * (VDP 2.6.0); a fresh packet for one key (VDP 2.12.0). */
void vdp_set_keyboard_locale(int locale);
void vdp_keyboard_control(int delay, int rate, int led);
void vdp_keyboard_cotrol(int delay, int rate, int led);   /* AgDev's spelling */
void vdp_control_keys(int on);
void vdp_request_key_state(int vkey);

/* The mouse (VDP 1.04): MOS keeps its state in the sysvars. */
void vdp_mouse_enable(void);
void vdp_mouse_disable(void);
void vdp_mouse_reset(void);
void vdp_mouse_set_cursor(int cursor);  /* 0-18 built in, a bitmap's ID, 65535 hidden */
void vdp_mouse_set_position(int x, int y);
void vdp_mouse_sample_rate(int rate);   /* 10, 20, 40, 60, 80, 100 or 200 a second */
void vdp_mouse_resolution(int resolution);  /* 0-3: 1, 2, 4, 8 counts per mm */
void vdp_mouse_scaling(int scaling);    /* 1 for 1:1, 2 for 1:2 */
void vdp_mouse_acceleration(int acceleration);
void vdp_mouse_wheel_accel(long acceleration);  /* 24 bits */

/* Characters: 0-255 from eight rows, by arguments or from data (VDP
 * 2.3.0); the system font back; a character drawn as a bitmap (VDP
 * 2.4.0). */
void vdp_redefine_character_special(int c, int b0, int b1, int b2, int b3, int b4, int b5, int b6, int b7);
void vdp_define_character(int c, const unsigned char *data);
void vdp_reset_system_font(void);
void vdp_map_char_to_bitmap(int c, int bitmap);

/* Viewports and the origin from the last graphics points (VDP 2.8.0). */
void vdp_set_text_viewport_via_plot(void);
void vdp_set_graphics_viewport_via_plot(void);
void vdp_set_graphics_origin_via_plot(void);
void vdp_move_graphics_origin_and_viewport(void);

/* The rest. */
void vdp_set_affine_transform(int flags, int buffer);  /* experimental (VDP 2.9.0) */
void vdp_page_mode_once(void);          /* paged mode until the output stops (VDP 2.14.0) */
void vdp_print_buffer(int buffer);      /* a buffer's bytes as characters (VDP 2.9.0) */
void vdp_logical_scr_dims(int on);      /* 1280x1024 graphics units, or pixels from the top left */
void vdp_legacy_modes(int on);          /* the modes of VDPs before 1.04 */
void vdp_swap(void);                    /* swap screen buffers, or wait for the next frame */
void vdp_flush_drawing_commands(void);  /* (VDP 2.8.0) */
void vdp_set_dash_pattern_length(int n);   /* 1-64; 0 the default (VDP 2.7.0) */
void vdp_set_variable(int id, int value);  /* a VDP variable (VDP 2.9.0) */
void vdp_clear_variable(int id);
void vdp_console_mode(int on);          /* echo VDU bytes to the USB serial port */
void vdp_terminal_mode(void);           /* the VDP as a VT100 terminal */

/* ---- bitmaps and sprites, VDU 23, 27, n (vdpbmp.c) ------------------------------------------ */

/* Bitmap n (8 bits) is buffer 64000+n; the "adv" commands take a 16-bit
 * buffer ID (VDP 2.2.0). Sprites, numbered 0-255, move by pixels. */
void vdp_select_bitmap(int n);
void vdp_load_bitmap(int width, int height, const unsigned long *data);  /* RGBA8888 */
int vdp_load_bitmap_file(const char *filename, int width, int height);  /* 0, or -1 */
void vdp_capture_bitmap(int n);         /* the screen between the last two points (VDP 2.2.0) */
void vdp_solid_bitmap(int width, int height, int r, int g, int b, int a);
void vdp_draw_bitmap(int x, int y);     /* at pixel x, y; PLOT &E8-&EF obeys viewports */
void vdp_adv_select_bitmap(int buffer);
/* The selected buffer as a bitmap: format 0 RGBA8888, 1 RGBA2222, 2 mono. */
void vdp_adv_bitmap_from_buffer(int width, int height, int format);
void vdp_adv_capture_bitmap(int buffer);

void vdp_select_sprite(int n);
void vdp_clear_sprite(void);            /* no frames */
void vdp_add_sprite_bitmap(int n);      /* bitmap n as the next frame */
void vdp_activate_sprites(int n);       /* sprites 0 to n-1 are drawn */
void vdp_next_sprite_frame(void);
void vdp_prev_sprite_frame(void);
void vdp_nth_sprite_frame(int n);
void vdp_show_sprite(void);
void vdp_hide_sprite(void);
void vdp_move_sprite_to(int x, int y);
void vdp_move_sprite_by(int x, int y);
void vdp_refresh_sprites(void);         /* software sprites move on the screen now */
void vdp_reset_sprites(void);           /* bitmaps and sprites */
void vdp_reset_sprites_only(void);
void vdp_set_sprite_paint_mode(int mode);   /* a GCOL mode (VDP 2.6.0) */
void vdp_set_sprite_hardware(void);     /* (VDP 2.12.0) */
void vdp_set_sprite_software(void);
void vdp_replace_sprite_frame(int n);   /* (VDP 2.12.0) */
void vdp_adv_add_sprite_bitmap(int buffer);
void vdp_adv_replace_sprite_frame(int buffer);
void vdp_create_sprite(int sprite, int first, int frames);   /* frames from bitmaps first, ... */
void vdp_adv_create_sprite(int sprite, int first, int frames);
void vdp_mouse_cursor_from_bitmap(int hot_x, int hot_y);   /* the selected bitmap */

/* ---- audio, VDU 23, 0, &85 (vdpaudio.c; VDP 1.04) ---------------------------------------------- */

/* Volume 0-127, frequency in Hz, durations in ms (65535: until silenced);
 * sample lengths and positions 24 bits. A negative sample number names a
 * sample, not a channel. Channel 255 means the whole sound system where a
 * comment says so. */
void vdp_audio_play_note(int channel, int volume, int frequency, int duration);
/* The channel's state (bits 1 active, 2 playing, 4 indefinite, 8 volume
 * envelope, 16 frequency envelope; 255 disabled), or -1 if no answer. */
int vdp_audio_status(int channel);
void vdp_audio_set_volume(int channel, int volume);      /* 255: overall (VDP 2.5.0) */
void vdp_audio_set_frequency(int channel, int frequency);
/* 0 square, 1 triangle, 2 sawtooth, 3 sine, 4 noise, 5 VIC noise;
 * negative: a sample by number. */
void vdp_audio_set_waveform(int channel, int waveform);
void vdp_audio_set_sample(int channel, int buffer);      /* the sample in a buffer */
void vdp_audio_load_sample(int sample, long length, const unsigned char *data);  /* 8-bit signed, 16 kHz */
void vdp_audio_clear_sample(int sample);
/* A buffer as a sample: format 0 8-bit signed, 1 unsigned, +16 tuneable;
 * and with its sample rate (VDP 2.2.0). */
void vdp_audio_create_sample_from_buffer(int channel, int buffer, int format);
void vdp_audio_create_sample_from_buffer_rate(int channel, int buffer, int format, int rate);
void vdp_audio_set_sample_frequency(int sample, int frequency);   /* (VDP 2.2.0, and the rest) */
void vdp_audio_set_buffer_frequency(int channel, int buffer, int frequency);
void vdp_audio_set_sample_repeat_start(int sample, long start);
void vdp_audio_set_buffer_repeat_start(int channel, int buffer, long start);
void vdp_audio_set_sample_repeat_length(int sample, long length);
void vdp_audio_set_buffer_repeat_length(int channel, int buffer, long length);
void vdp_audio_volume_envelope_disable(int channel);
void vdp_audio_volume_envelope_ADSR(int channel, int attack, int decay, int sustain, int release);
/* Multi-phase (VDP 2.5.0): its start only (send the rest with vdu_n), or
 * whole from n bytes of counts and level, duration; pairs. */
void vdp_audio_volume_envelope_multiphase_ADSR(int channel);
void vdp_audio_volume_envelope_multiphase(int channel, const unsigned char *data, int n);
void vdp_audio_frequency_envelope_disable(int channel);
/* Stepped: its start only (phase_count pairs of adjustment; steps; follow
 * with vdu_n), or whole from 2 * phase_count values. control: 1 repeat,
 * 2 cumulative, 4 restricted to 0-65535. */
void vdp_audio_frequency_envelope_stepped(int channel, int phase_count, int control, int step_length);
void vdp_audio_frequency_envelope(int channel, int control, int step_length, const int *phases, int phase_count);
void vdp_audio_enable_channel(int channel);   /* up to 32; 0-2 are on at start-up */
void vdp_audio_disable_channel(int channel);
void vdp_audio_reset_channel(int channel);
void vdp_audio_sample_seek(int channel, long position);
void vdp_audio_sample_duration(int channel, long duration);
void vdp_audio_sample_rate(int channel, int rate);        /* 255: overall */
/* parameter: 0 duty cycle, 2 volume, 3 frequency's low byte, &83 frequency
 * (16 bits: any parameter with &80 sends 16 bits) (VDP 2.5.0). */
void vdp_audio_set_waveform_parameter(int channel, int parameter, int value);

/* ---- buffers, VDU 23, 0, &A0 (vdpbuf.c; VDP 1.04) ---------------------------------------------- */

/* Buffer IDs are 16 bits; 65535 is "this buffer" inside a buffered
 * sequence. Offsets for commands 9-12 are 24 bits; the _block forms add a
 * block number. The commands with variable arguments have their start
 * here (send the rest with vdu_n), and vdp_adv_command sends any command
 * with argument bytes you build. */
void vdp_adv_command(int buffer, int command, const unsigned char *args, int n);
void vdp_adv_write_block(int buffer, int length);      /* the next length bytes as a block */
void vdp_adv_write_block_data(int buffer, int length, const char *data);
void vdp_adv_call_buffer(int buffer);                  /* run its VDU commands */
void vdp_adv_clear_buffer(int buffer);                 /* 65535: all buffers */
void vdp_adv_create(int buffer, int length);           /* a writeable buffer, zeroed */
void vdp_adv_stream(int buffer);                       /* replies to a buffer (avoid) */
void vdp_adv_adjust(int buffer, int operation, int offset);
void vdp_adv_call_conditional(int buffer, int operation, int check_buffer, int check_offset);
void vdp_adv_jump_buffer(int buffer);
void vdp_adv_jump_conditional(int buffer, int operation, int check_buffer, int check_offset);
void vdp_adv_jump_offset(int buffer, long offset);
void vdp_adv_jump_offset_block(int buffer, long offset, int block);
void vdp_adv_jump_offset_conditional(int buffer, long offset);
void vdp_adv_jump_offset_block_conditional(int buffer, long offset, int block);
void vdp_adv_call_offset(int buffer, long offset);
void vdp_adv_call_offset_block(int buffer, long offset, int block);
void vdp_adv_call_offset_conditional(int buffer, long offset);
void vdp_adv_call_offset_block_conditional(int buffer, long offset, int block);
void vdp_adv_copy_multiple(int target, int count, ...);   /* count source IDs follow */
void vdp_adv_copy_blocks(int target, const int *sources, int count);
void vdp_adv_consolidate(int buffer);
void vdp_adv_split(int buffer, int block_size);
void vdp_adv_split_multiple(int buffer, int block_size, int count, ...);
void vdp_adv_split_multiple_from(int buffer, int block_size, int target);
void vdp_adv_split_by_width(int buffer, int width, int block_count);
void vdp_adv_split_by_width_multiple(int buffer, int width, int count, ...);
void vdp_adv_split_by_width_multiple_from(int buffer, int width, int block_count, int target);
void vdp_adv_spread_multiple(int buffer, int count, ...);
void vdp_adv_spread_multiple_from(int buffer, int target);
void vdp_adv_reverse_block_order(int buffer);
/* options: 1 16-bit values, 2 32-bit, 3 value_size bytes, 4 within chunks
 * of chunk_size, 8 the block order too. */
void vdp_adv_reverse_block_data(int buffer, int options, int value_size, int chunk_size);
void vdp_adv_copy_multiple_by_reference(int target, int count, ...);
void vdp_adv_copy_multiple_consolidate(int target, int count, ...);
void vdp_adv_compress_buffer(int target, int source);
void vdp_adv_decompress_buffer(int target, int source);
/* Run a buffer at an event: 0 each frame, 1 a mode change, 2 a key, 3 the
 * mouse, 4 a palette change, 5 a pixel read (VDP 2.12.0; 2-5 2.15.0). */
void vdp_adv_set_callback(int buffer, int event);
void vdp_adv_remove_callback(int buffer, int event);
void vdp_adv_debug_info(int buffer);    /* to the VDP's USB serial console */

/* ---- contexts, fonts, copper, tiles (vdpmore.c) ----------------------------------------------------- */

/* Graphics contexts, VDU 23, 0, &C8 (VDP 2.8.0). Sent as documented, but
 * a known issue: on the emulator and on a real Agon, a text background
 * colour set after vdp_context_save did not show as documented. */
void vdp_context_select(int context);
void vdp_context_delete(int context);
void vdp_context_reset(int flags);
void vdp_context_save(void);
void vdp_context_restore(void);
void vdp_context_save_copy(int context);
void vdp_context_restore_all(void);
void vdp_context_clear_stack(void);

/* Fonts in buffers, VDU 23, 0, &95 (VDP 2.8.0); 65535 the system font. */
void vdp_font_select(int buffer, int flags);
void vdp_font_create(int buffer, int width, int height, int ascent, int flags);
void vdp_font_adjust(int buffer, int field, int value);
void vdp_font_delete(int buffer);
void vdp_font_copy(int buffer);         /* the system font into a buffer */

/* Copper palettes, VDU 23, 0, &C4 (VDP 2.12.0, behind VDP variable
 * &310): palettes switched as the screen is drawn. */
void vdp_copper_create_palette(int palette);
void vdp_copper_delete_palette(int palette);
void vdp_copper_set_palette_entry(int palette, int index, int red, int green, int blue);
void vdp_copper_set_signal_list(int buffer);   /* 16-bit pairs: rows, palette */
void vdp_copper_reset_signal_list(void);

/* The tile engine, VDU 23, 0, &C2 (VDP 2.12.0): 8x8 RGBA2222 tiles in
 * banks, a map of them, and a layer showing part of the map. */
void vdp_tile_bank_init(int bank);
void vdp_tile_load(int bank, int id, const unsigned char *pixels);   /* 64 bytes */
void vdp_tile_draw(int bank, int id, int x, int y, int x_offset, int y_offset, int attribute);
void vdp_tile_bank_free(int bank);
void vdp_tile_map_init(int size);       /* 0-8: 32x32 to 128x128 */
void vdp_tile_map_set(int x, int y, int id, int attribute);   /* bits 0-1 flips, 2-3 bank */
void vdp_tile_map_free(void);
void vdp_tile_layer_init(int size);     /* 0 80x60, 1 80x30, 2 40x30, 3 40x25 */
void vdp_tile_layer_scroll(int x, int y, int x_offset, int y_offset);
void vdp_tile_layer_update(void);
void vdp_tile_layer_draw(void);
void vdp_tile_layer_update_draw(void);

#endif
