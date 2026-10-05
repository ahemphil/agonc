/* vdpaudio.c - <agon/vdp.h>'s audio commands, VDU 23, 0, &85, channel,
 * command (VDP 1.04 and later; the sample commands past 5, 2 and the
 * commands from 11 on, Console8 VDP 2.2.0). In libagon.s.
 *
 * Volumes are 0-127, frequencies in Hz and durations in milliseconds
 * (65535: until silenced), all 16 bits; sample lengths and positions are
 * 24 bits (a long here). A negative sample number names a sample by
 * number rather than a channel (sample -1 lives in buffer 64256, -2 in
 * 64257, ...). Most commands answer with a status (1 success, 0 failure)
 * that MOS puts in MOS_SYSVAR->audio_success; vdp_audio_status waits for
 * it.
 *
 * Every command is VDU 23, 0, &85, channel, command, then the command's
 * arguments (vdp.c's opening comment explains the protocol). Command 5
 * (samples) and commands 6 and 7 (the envelopes) take a sub-command byte
 * next, which picks the form of what follows. A channel or sample number
 * is sent as one byte, so -1 and 255 are the same byte. Only
 * vdp_audio_status waits for the VDP's answer; the rest only send.
 */

#include <agon/vdp.h>
#include <agon/mos.h>

/* A 16-bit value, and a 24-bit one, low byte first (as in vdp.c). */
#define W(b, i, v) ((b)[i] = (char)((v) & 255), (b)[(i) + 1] = (char)((v) >> 8 & 255))
#define W3(b, i, v) ((b)[i] = (char)((v) & 255), (b)[(i) + 1] = (char)((v) >> 8 & 255), \
                     (b)[(i) + 2] = (char)((v) >> 16 & 255))

/* vdpsys.c: clear a reply flag, send, wait for the flag. */
int __vdp_ask(const char *cmd, int n, int bit, int wait);

/* The five bytes every audio command starts with. */
static void head(char *b, int channel, int command)
{
    b[0] = 23;
    b[1] = 0;
    b[2] = 0x85;
    b[3] = channel;
    b[4] = command;
}

/* A command with no arguments. */
static void cmd0(int channel, int command)
{
    char b[5];

    head(b, channel, command);
    vdu_n(b, 5);
}

/* A command with one byte. */
static void cmd1(int channel, int command, int a)
{
    char b[6];

    head(b, channel, command);
    b[5] = a;
    vdu_n(b, 6);
}

/* A command with one 16-bit value. */
static void cmdw(int channel, int command, int a)
{
    char b[7];

    head(b, channel, command);
    W(b, 5, a);
    vdu_n(b, 7);
}

/* A sample command (5, sub) with a 24-bit value, after a buffer ID if
 * with_buffer; i tracks where the value goes, and the length sent. */
static void sample24(int channel, int sub, int with_buffer, int buffer, long value)
{
    char b[11];
    int i;

    head(b, channel, 5);
    b[5] = sub;
    i = 6;
    if (with_buffer) {
        W(b, 6, buffer);
        i = 8;
    }
    W3(b, i, value);
    vdu_n(b, i + 3);
}

/* Command 0: volume, frequency; duration;. */
void vdp_audio_play_note(int channel, int volume, int frequency, int duration)
{
    char b[10];

    head(b, channel, 0);
    b[5] = volume;
    W(b, 6, frequency);
    W(b, 8, duration);
    vdu_n(b, 10);
}

/* The channel's status bits (1 active, 2 playing, 4 indefinite, 8 volume
 * envelope, 16 frequency envelope; 255 not enabled), or -1 if the VDP did
 * not answer within a second. Command 1; the answer lands in
 * audio_success and sets VDP_PFLAG_AUDIO. */
int vdp_audio_status(int channel)
{
    char b[5];

    head(b, channel, 1);
    if (__vdp_ask(b, 5, VDP_PFLAG_AUDIO, 1) < 0)
        return -1;
    return MOS_SYSVAR->audio_success;
}

/* Channel 255 (-1): the whole sound system's volume (VDP 2.5.0). */
void vdp_audio_set_volume(int channel, int volume)
{
    cmd1(channel, 2, volume);
}

void vdp_audio_set_frequency(int channel, int frequency)
{
    cmdw(channel, 3, frequency);
}

/* 0 square, 1 triangle, 2 sawtooth, 3 sine, 4 noise, 5 VIC noise; a
 * negative value, a sample by number. */
void vdp_audio_set_waveform(int channel, int waveform)
{
    cmd1(channel, 4, waveform);
}

/* The sample in a buffer as the channel's waveform (waveform 8). */
void vdp_audio_set_sample(int channel, int buffer)
{
    char b[8];

    head(b, channel, 4);
    b[5] = 8;
    W(b, 6, buffer);
    vdu_n(b, 8);
}

/* Sample (a negative number) from length bytes of 8-bit signed PCM at
 * 16 kHz. Buffers (vdp_adv_write_block_data, then
 * vdp_audio_create_sample_from_buffer) are the better way for long ones. */
void vdp_audio_load_sample(int sample, long length, const unsigned char *data)
{
    char b[9];

    head(b, sample, 5);
    b[5] = 0;
    W3(b, 6, length);
    vdu_n(b, 9);
    /* vdu_n's count is 24 bits too, but sending in 4096-byte pieces puts a
     * Ctrl-C check (in vdu_n) between them. */
    while (length > 0) {
        vdu_n((const char *)data, length > 4096 ? 4096 : (int)length);
        data = data + 4096;
        length = length - 4096;
    }
}

void vdp_audio_clear_sample(int sample)
{
    char b[6];

    head(b, sample, 5);
    b[5] = 1;
    vdu_n(b, 6);
}

/* Sample sub-command 2: the buffer's data as a sample. format: 0 8-bit
 * signed, 1 unsigned; plus 16 for tuneable. */
void vdp_audio_create_sample_from_buffer(int channel, int buffer, int format)
{
    char b[9];

    head(b, channel, 5);
    b[5] = 2;
    W(b, 6, buffer);
    b[8] = format;
    vdu_n(b, 9);
}

/* The same with its sample rate (format gets 8, "rate follows"). */
void vdp_audio_create_sample_from_buffer_rate(int channel, int buffer, int format, int rate)
{
    char b[11];

    head(b, channel, 5);
    b[5] = 2;
    W(b, 6, buffer);
    b[8] = format | 8;
    W(b, 9, rate);
    vdu_n(b, 11);
}

/* The frequency the sample's data represents, for tuning it (sample
 * sub-command 3; 4 is the same for a sample in a buffer). */
void vdp_audio_set_sample_frequency(int sample, int frequency)
{
    char b[8];

    head(b, sample, 5);
    b[5] = 3;
    W(b, 6, frequency);
    vdu_n(b, 8);
}

void vdp_audio_set_buffer_frequency(int channel, int buffer, int frequency)
{
    char b[10];

    head(b, channel, 5);
    b[5] = 4;
    W(b, 6, buffer);
    W(b, 8, frequency);
    vdu_n(b, 10);
}

/* Sample sub-commands 5 to 8: the repeat start and length, by sample
 * number or by buffer, each a 24-bit byte count. */
void vdp_audio_set_sample_repeat_start(int sample, long start)
{
    sample24(sample, 5, 0, 0, start);
}

void vdp_audio_set_buffer_repeat_start(int channel, int buffer, long start)
{
    sample24(channel, 6, 1, buffer, start);
}

/* -1 (the default): to the end of the sample. */
void vdp_audio_set_sample_repeat_length(int sample, long length)
{
    sample24(sample, 7, 0, 0, length);
}

void vdp_audio_set_buffer_repeat_length(int channel, int buffer, long length)
{
    sample24(channel, 8, 1, buffer, length);
}

/* Command 6 (volume envelope), type 0: none. */
void vdp_audio_volume_envelope_disable(int channel)
{
    cmd1(channel, 6, 0);
}

/* Command 6, type 1: attack, decay, release in milliseconds; sustain a
 * level scaled by volume/127. */
void vdp_audio_volume_envelope_ADSR(int channel, int attack, int decay, int sustain, int release)
{
    char b[13];

    head(b, channel, 6);
    b[5] = 1;
    W(b, 6, attack);
    W(b, 8, decay);
    b[10] = sustain;
    W(b, 11, release);
    vdu_n(b, 13);
}

/* A multi-phase envelope's opening bytes only (AgDev's form): the counts
 * and phases follow with vdu_n. */
void vdp_audio_volume_envelope_multiphase_ADSR(int channel)
{
    char b[6];

    head(b, channel, 6);
    b[5] = 2;
    vdu_n(b, 6);
}

/* The whole multi-phase envelope (VDP 2.5.0): data holds n bytes, the
 * attack count and its level, duration; pairs, then the sustain's, then
 * the release's, as the VDP takes them. */
void vdp_audio_volume_envelope_multiphase(int channel, const unsigned char *data, int n)
{
    vdp_audio_volume_envelope_multiphase_ADSR(channel);
    vdu_n((const char *)data, n);
}

/* Command 7 (frequency envelope), type 0: none. */
void vdp_audio_frequency_envelope_disable(int channel)
{
    cmd1(channel, 7, 0);
}

/* A stepped envelope's opening bytes (AgDev's form): phase_count pairs of
 * adjustment; steps; follow with vdu_n. control: 1 repeat, 2 cumulative,
 * 4 restrict to 0-65535. */
void vdp_audio_frequency_envelope_stepped(int channel, int phase_count, int control, int step_length)
{
    char b[10];

    head(b, channel, 7);
    b[5] = 1;
    b[6] = phase_count;
    b[7] = control;
    W(b, 8, step_length);
    vdu_n(b, 10);
}

/* The whole stepped envelope: phases holds 2 * phase_count values,
 * adjustment and steps for each phase, each sent as 16 bits. The values
 * are ints in C, so the array is converted a phase at a time, through a
 * 4-byte buffer. */
void vdp_audio_frequency_envelope(int channel, int control, int step_length, const int *phases, int phase_count)
{
    char b[4];
    int i;

    vdp_audio_frequency_envelope_stepped(channel, phase_count, control, step_length);
    for (i = 0; i < phase_count; i++) {
        W(b, 0, phases[2 * i]);
        W(b, 2, phases[2 * i + 1]);
        vdu_n(b, 4);
    }
}

/* Commands 8, 9 and 10. Channels 0-2 are enabled at start-up; up to 32
 * can be. */
void vdp_audio_enable_channel(int channel)
{
    cmd0(channel, 8);
}

void vdp_audio_disable_channel(int channel)
{
    cmd0(channel, 9);
}

void vdp_audio_reset_channel(int channel)
{
    cmd0(channel, 10);
}

/* To a byte offset in the sample being played. */
void vdp_audio_sample_seek(int channel, long position)
{
    char b[8];

    head(b, channel, 11);
    W3(b, 5, position);
    vdu_n(b, 8);
}

/* The playing note's duration (or a new note's), 24 bits. */
void vdp_audio_sample_duration(int channel, long duration)
{
    char b[8];

    head(b, channel, 12);
    W3(b, 5, duration);
    vdu_n(b, 8);
}

/* Channel 255 (-1): the whole sound system's sample rate. */
void vdp_audio_sample_rate(int channel, int rate)
{
    cmdw(channel, 13, rate);
}

/* parameter: 0 duty cycle, 2 volume, 3 frequency (low 8 bits), &83
 * frequency (16 bits); with &80 set the value is sent as 16 bits.
 * Command 14. Both bytes are always built; the length sent, 7 or 8,
 * decides whether the high one goes. */
void vdp_audio_set_waveform_parameter(int channel, int parameter, int value)
{
    char b[8];

    head(b, channel, 14);
    b[5] = parameter;
    W(b, 6, value);
    vdu_n(b, (parameter & 0x80) ? 8 : 7);
}
