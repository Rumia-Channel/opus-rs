/* Differential PLC test: encode with libopus, dump packets, decode N good + 1 lost,
 * output C PLC decode for comparison with Rust. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "opus.h"

int main(int argc, char *argv[]) {
    int sr = atoi(argv[1]);
    int ch = atoi(argv[2]);
    int n_good = atoi(argv[3]);
    const char *prefix = argv[4];
    int fs = sr / 50;
    int err;

    OpusEncoder *enc = opus_encoder_create(sr, ch, OPUS_APPLICATION_VOIP, &err);
    opus_encoder_ctl(enc, OPUS_SET_BITRATE(sr == 16000 ? 24000 : 16000));
    OpusDecoder *dec = opus_decoder_create(sr, ch, &err);

    float input[640], pcm[640];
    unsigned char pkt[4000];

    char pfname[256];
    snprintf(pfname, sizeof(pfname), "%s_packets.bin", prefix);
    FILE *fp = fopen(pfname, "wb");

    for (int f = 0; f < n_good; f++) {
        for (int i = 0; i < fs; i++) {
            double t = (double)(f * fs + i) / sr;
            double amp = 0.25 + 0.15 * sin(2 * M_PI * 2 * t);
            double sig = amp * sin(2 * M_PI * 220 * t)
                       + 0.1 * sin(2 * M_PI * 440 * t)
                       + 0.05 * sin(2 * M_PI * 660 * t);
            input[i] = (float)sig;
        }
        int n = opus_encode_float(enc, input, fs, pkt, 4000);
        unsigned char lb[2] = { n & 0xFF, (n >> 8) & 0xFF };
        fwrite(lb, 1, 2, fp);
        fwrite(pkt, 1, n, fp);
        int d = opus_decode_float(dec, pkt, n, pcm, fs, 0);
        if (f == n_good - 1) {
            snprintf(pfname, sizeof(pfname), "%s_good.pcm", prefix);
            FILE *fo = fopen(pfname, "wb");
            fwrite(pcm, sizeof(float), d * ch, fo);
            fclose(fo);
        }
    }
    fclose(fp);

    unsigned char lost[1] = { 0x48 };
    int d = opus_decode_float(dec, lost, 1, pcm, fs, 0);
    snprintf(pfname, sizeof(pfname), "%s_plc.pcm", prefix);
    FILE *fo = fopen(pfname, "wb");
    fwrite(pcm, sizeof(float), d * ch, fo);
    fclose(fo);
    fprintf(stderr, "Wrote %d packets, C PLC frame: %d samples\n", n_good, d);
    return 0;
}
