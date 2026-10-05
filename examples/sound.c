/* sound.c - the VDP's sound system through <agon/vdp.h>: notes, the
 * waveforms, a chord on three channels and a volume envelope.
 *
 *     agonc -o sound.bin sound.c
 *     sound
 *
 * The VDP plays a note on its own once asked, so the program waits for
 * each note to finish by watching MOS's clock, which counts hundredths of
 * a second. Volumes are 0 to 127, frequencies in hertz, durations in
 * milliseconds. Channels 0, 1 and 2 are ready when the Agon starts.
 */

#include <stdio.h>
#include <agon/vdp.h>
#include <agon/mos.h>

/* Wait cs hundredths of a second. MOS's clock is a 32-bit count, so the
 * subtraction works even when it wraps. */
static void wait_cs(int cs)
{
    unsigned long start;

    start = MOS_SYSVAR->time;
    while (MOS_SYSVAR->time - start < (unsigned long)cs)
        ;
}

/* The C major scale, from middle C, in hertz. */
static const int scale[] = { 262, 294, 330, 349, 392, 440, 494, 523 };

int main(void)
{
    static const char *const names[] = { "square", "triangle", "sawtooth", "sine" };
    int i, w;

    printf("A scale on channel 0\n");
    for (i = 0; i < 8; i++) {
        vdp_audio_play_note(0, 100, scale[i], 250);
        wait_cs(30);                    /* the note's 250 ms, and a gap */
    }

    /* The same note in each of the four waveforms: each has its own
     * tone colour. */
    for (w = 0; w < 4; w++) {
        printf("A in a %s wave\n", names[w]);
        vdp_audio_set_waveform(0, w);
        vdp_audio_play_note(0, 100, 440, 600);
        wait_cs(70);
    }
    vdp_audio_set_waveform(0, 0);

    /* Three channels at once: C, E and G make a chord. */
    printf("A chord on channels 0, 1 and 2\n");
    vdp_audio_play_note(0, 80, 262, 1500);
    vdp_audio_play_note(1, 80, 330, 1500);
    vdp_audio_play_note(2, 80, 392, 1500);
    wait_cs(170);

    /* An envelope shapes a note's volume over time: a quick attack
     * (10 ms), a decay (200 ms) to a sustain level (40 of 127), then a
     * long release (800 ms) after the note's own duration. */
    printf("A plucked note, with an envelope\n");
    vdp_audio_volume_envelope_ADSR(0, 10, 200, 40, 800);
    vdp_audio_play_note(0, 120, 330, 300);
    wait_cs(130);
    vdp_audio_volume_envelope_disable(0);

    printf("Done\n");
    return 0;
}
