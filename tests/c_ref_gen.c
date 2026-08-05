/* C reference harness: encode+decode with libopus, dump packets + PCM for Rust comparison */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "opus.h"

#define FRAME_SIZE 960  /* 20ms at 48kHz */

int main(int argc, char *argv[]) {
    if (argc < 6) {
        fprintf(stderr, "Usage: %s <mode(silk|hybrid|celt|stereo_silk)> <outfile_packets> <outfile_pcm> <n_frames> <channels>\n", argv[0]);
        return 1;
    }

    const char *mode = argv[1];
    const char *pkt_file = argv[2];
    const char *pcm_file = argv[3];
    int n_frames = atoi(argv[4]);
    int channels = atoi(argv[5]);

    int sampling_rate = 48000;
    int application;
    int bitrate;

    if (strcmp(mode, "silk") == 0) {
        sampling_rate = 16000;
        application = OPUS_APPLICATION_VOIP;
        bitrate = 24000;
    } else if (strcmp(mode, "stereo_silk") == 0) {
        sampling_rate = 16000;
        channels = 2;
        application = OPUS_APPLICATION_VOIP;
        bitrate = 32000;
    } else if (strcmp(mode, "hybrid") == 0) {
        sampling_rate = 48000;
        application = OPUS_APPLICATION_AUDIO;
        bitrate = 32000;
    } else if (strcmp(mode, "celt_stereo") == 0) {
        sampling_rate = 48000;
        channels = 2;
        application = OPUS_APPLICATION_RESTRICTED_LOWDELAY;
        bitrate = 64000;
    } else {
        /* celt */
        sampling_rate = 48000;
        application = OPUS_APPLICATION_RESTRICTED_LOWDELAY;
        bitrate = 64000;
    }

    int frame_size = sampling_rate / 50; /* 20ms */
    int err;

    OpusEncoder *enc = opus_encoder_create(sampling_rate, channels, application, &err);
    if (err != OPUS_OK) { fprintf(stderr, "encoder create: %s\n", opus_strerror(err)); return 1; }
    opus_encoder_ctl(enc, OPUS_SET_BITRATE(bitrate));
    opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(10));

    OpusDecoder *dec = opus_decoder_create(sampling_rate, channels, &err);
    if (err != OPUS_OK) { fprintf(stderr, "decoder create: %s\n", opus_strerror(err)); return 1; }

    FILE *fp_pkt = fopen(pkt_file, "wb");
    FILE *fp_pcm = fopen(pcm_file, "wb");
    if (!fp_pkt || !fp_pcm) { fprintf(stderr, "Cannot open output files\n"); return 1; }

    /* Generate test signal: different per channel for stereo */
    float *input = malloc(frame_size * channels * sizeof(float));
    unsigned char packet[4000];
    float *pcm_out = malloc(frame_size * channels * sizeof(float));

    for (int f = 0; f < n_frames; f++) {
        /* Generate input signal */
        for (int i = 0; i < frame_size; i++) {
            double t = (double)(f * frame_size + i) / sampling_rate;
            for (int c = 0; c < channels; c++) {
                double freq = (c == 0) ? 440.0 : 660.0;
                input[i * channels + c] = (float)(sin(freq * t * 2.0 * M_PI) * 0.3);
            }
        }

        /* Encode */
        int n_bytes = opus_encode_float(enc, input, frame_size, packet, 4000);
        if (n_bytes < 0) { fprintf(stderr, "encode error: %s\n", opus_strerror(n_bytes)); return 1; }

        /* Write packet: [u16le length][packet bytes] */
        unsigned char len_buf[2];
        len_buf[0] = (n_bytes) & 0xFF;
        len_buf[1] = (n_bytes >> 8) & 0xFF;
        fwrite(len_buf, 1, 2, fp_pkt);
        fwrite(packet, 1, n_bytes, fp_pkt);

        /* Decode with C libopus (reference) */
        int decoded = opus_decode_float(dec, packet, n_bytes, pcm_out, frame_size, 0);
        if (decoded < 0) { fprintf(stderr, "decode error: %s\n", opus_strerror(decoded)); return 1; }

        /* Write reference PCM as f32le */
        fwrite(pcm_out, sizeof(float), decoded * channels, fp_pcm);
    }

    fclose(fp_pkt);
    fclose(fp_pcm);
    free(input);
    free(pcm_out);
    opus_encoder_destroy(enc);
    opus_decoder_destroy(dec);

    fprintf(stderr, "Generated %d frames of %s @ %dHz %dch\n", n_frames, mode, sampling_rate, channels);
    return 0;
}
