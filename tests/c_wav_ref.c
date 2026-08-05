/* C reference encode/decode: produces a WAV for A/B listening comparison
 * with opus-rs output.
 *
 * Build: cc -O2 -Iopus-1.6.1/include tests/c_wav_ref.c -o tests/c_wav_ref opus-1.6.1/libopus.a -lm
 * Usage: tests/c_wav_ref <input.wav> <output.wav> <app(voip|audio|rld)> <bitrate> <sampling_rate>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "opus.h"

static unsigned char* read_file(const char* path, int *len) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc(*len);
    fread(buf, 1, *len, f);
    fclose(f);
    return buf;
}

/* Parse simple WAV (16-bit PCM mono/stereo) → interleaved i16 samples */
static short* parse_wav(unsigned char* data, int len, int *sr, int *ch, int *nsamples) {
    /* Find fmt and data chunks */
    int p = 12; /* skip RIFF header */
    int bits=16, audio_fmt=1;
    *ch = 1; *sr = 16000;
    while (p + 8 <= len) {
        int cid = *(unsigned int*)(data+p);
        int csz = *(unsigned int*)(data+p+4);
        if (cid == 0x20746d66) { /* "fmt " */
            audio_fmt = *(short*)(data+p+8+0);
            *ch = *(short*)(data+p+8+2);
            *sr = *(int*)(data+p+8+4);
            bits = *(short*)(data+p+8+14);
        } else if (cid == 0x61746164) { /* "data" */
            *nsamples = csz / (*ch * bits/8);
            return (short*)(data+p+8);
        }
        p += 8 + csz + (csz & 1);
    }
    return NULL;
}

static void write_wav(const char* path, int sr, int ch, short* pcm, int n) {
    FILE *f = fopen(path, "wb");
    int dsz = n * ch * 2;
    int fsz = 36 + dsz;
    fwrite("RIFF", 1, 4, f); fwrite(&fsz, 4, 1, f); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    int fmtsz = 16; fwrite(&fmtsz, 4, 1, f);
    short af=1; fwrite(&af, 2, 1, f); fwrite(&ch, 2, 1, f);
    fwrite(&sr, 4, 1, f);
    int br = sr*ch*2; fwrite(&br, 4, 1, f);
    short ba = ch*2; fwrite(&ba, 2, 1, f);
    short bps = 16; fwrite(&bps, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&dsz, 4, 1, f);
    fwrite(pcm, 2, n*ch, f);
    fclose(f);
    fprintf(stderr, "Wrote %s: %dHz %dch %d samples\n", path, sr, ch, n);
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <input.wav> <output.wav> [app] [bitrate] [sr]\n", argv[0]);
        fprintf(stderr, "  app: voip|audio|rld (default: voip)\n");
        fprintf(stderr, "  bitrate: bps (default: 24000)\n");
        fprintf(stderr, "  sr: output rate (default: input rate)\n");
        return 1;
    }

    int len;
    unsigned char *raw = read_file(argv[1], &len);
    int sr, ch, nsamples;
    short *pcm_in = parse_wav(raw, len, &sr, &ch, &nsamples);
    if (!pcm_in) { fprintf(stderr, "No data chunk found\n"); return 1; }

    const char *app_s = argc > 3 ? argv[3] : "voip";
    int bitrate = argc > 4 ? atoi(argv[4]) : 24000;
    int out_sr = argc > 5 ? atoi(argv[5]) : sr;
    int app;
    if (!strcmp(app_s, "audio")) app = OPUS_APPLICATION_AUDIO;
    else if (!strcmp(app_s, "rld")) app = OPUS_APPLICATION_RESTRICTED_LOWDELAY;
    else app = OPUS_APPLICATION_VOIP;

    int err;
    OpusEncoder *enc = opus_encoder_create(sr, ch, app, &err);
    if (err != OPUS_OK) { fprintf(stderr, "enc: %s\n", opus_strerror(err)); return 1; }
    opus_encoder_ctl(enc, OPUS_SET_BITRATE(bitrate));
    opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(10));

    OpusDecoder *dec = opus_decoder_create(out_sr, ch, &err);
    if (err != OPUS_OK) { fprintf(stderr, "dec: %s\n", opus_strerror(err)); return 1; }

    int fs = sr / 50; /* 20ms */
    int out_fs = out_sr / 50;

    short *pcm_out = malloc(nsamples * ch * 2 * (out_sr / sr + 1));
    int out_n = 0;
    unsigned char packet[4000];

    for (int i = 0; i + fs <= nsamples; i += fs) {
        int n = opus_encode(enc, pcm_in + i * ch, fs, packet, 4000);
        if (n < 0) { fprintf(stderr, "encode: %s\n", opus_strerror(n)); break; }
        int d = opus_decode(dec, packet, n, pcm_out + out_n * ch, out_fs, 0);
        if (d < 0) { fprintf(stderr, "decode: %s\n", opus_strerror(d)); break; }
        out_n += d;
    }

    write_wav(argv[2], out_sr, ch, pcm_out, out_n);

    free(pcm_out);
    opus_encoder_destroy(enc);
    opus_decoder_destroy(dec);
    free(raw);
    return 0;
}
