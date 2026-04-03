#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <omp.h>
#include <x86intrin.h>
#include "shared.h"

typedef struct {
    uint32_t orig_len, rle_len, bit_len, bwt_idx;
    uint8_t *payload, *output;
} Chunk;

static inline void fast_mtf_rle(uint16_t *rle, uint32_t rle_len, uint8_t *bbuf) {
    uint8_t alpha[256];
    for (int i = 0; i < 256; i++) alpha[i] = (uint8_t)i;
    uint32_t bp = 0;
    
    for (uint32_t i = 0; i < rle_len; ) {
        uint16_t sym = rle[i++];
        if (sym > 1) {
            uint8_t s = alpha[sym - 1];
            bbuf[bp++] = s;
            for (int j = sym - 1; j > 0; j--) {
                alpha[j] = alpha[j - 1];
            }
            alpha[0] = s;
            continue;
        }
        uint32_t run = sym + 1, p = 2;
        while (i < rle_len && rle[i] <= 1) {
            run += (rle[i++] + 1) * p;
            p <<= 1;
        }
        memset(bbuf + bp, alpha[0], run);
        bp += run;
    }
}

void decompress_chunk(Chunk *c) {
    AdaptiveModel model;
    model_init(&model);
    AC_Decoder dec = { .data = c->payload, .bit_ptr = 0, .bit_limit = c->bit_len, .low = 0, .high = TOP_VALUE, .value = 0 };

    for (int i = 0; i < 32; i++) {
        int bit = (dec.bit_ptr < dec.bit_limit) ? (c->payload[dec.bit_ptr >> 3] >> (7 - (dec.bit_ptr & 7))) & 1 : 0;
        dec.value = (dec.value << 1) | bit;
        dec.bit_ptr++;
    }

    uint16_t *rle = malloc(c->rle_len * sizeof(uint16_t));
    for (uint32_t i = 0; i < c->rle_len; i++) {
        rle[i] = ac_decode_symbol(&dec, &model);
        model_update(&model, rle[i]);
    }

    uint32_t blen = c->orig_len + 1;
    uint8_t *bbuf = malloc(blen);
    fast_mtf_rle(rle, c->rle_len, bbuf);
    free(rle);

    uint32_t *t = malloc(blen * sizeof(uint32_t));
    uint32_t cnt[256] = {0}, strt[256] = {0};
    
    for (uint32_t i = 0; i < blen; i++) cnt[bbuf[i]]++;
    for (uint32_t i = 1; i < 256; i++) strt[i] = strt[i - 1] + cnt[i - 1];
    for (uint32_t i = 0; i < blen; i++) t[strt[bbuf[i]]++] = i;

    uint32_t curr = t[c->bwt_idx];
    uint8_t *dst = c->output;
    uint32_t n = c->orig_len;


    for (uint32_t i = 0; i < n; i++) {
        _mm_prefetch((const char*)&t[t[curr]], _MM_HINT_T0);
        dst[i] = bbuf[curr];
        curr = t[curr];
    }

    free(bbuf);
    free(t);
}

int main(int argc, char **argv) {
    if (argc < 3) return 1;
    FILE *fin = fopen(argv[1], "rb");
    if (!fin) return 1;

    fseek(fin, 0, SEEK_END);
    long fsize = ftell(fin);
    rewind(fin);

    uint8_t *buf = malloc(fsize);
    if (fread(buf, 1, fsize, fin) != (size_t)fsize) return 1;
    fclose(fin);

    uint32_t nch = 0, cap = 16;
    Chunk *chunks = malloc(sizeof(Chunk) * cap);
    long pos = 0;

    // Fast header parsing
    while (pos < fsize) {
        if (nch >= cap) {
            cap *= 2;
            chunks = realloc(chunks, sizeof(Chunk) * cap);
        }
        uint32_t *h = (uint32_t*)(buf + pos);
        chunks[nch].orig_len = h[0];
        chunks[nch].rle_len = h[1];
        chunks[nch].bit_len = h[2];
        chunks[nch].bwt_idx = h[3];
        chunks[nch].payload = buf + pos + 16;
        chunks[nch].output = malloc(chunks[nch].orig_len);
        pos += 16 + (chunks[nch].bit_len + 7) / 8;
        nch++;
    }

    double start = omp_get_wtime();

    #pragma omp parallel for schedule(static)
    for (uint32_t i = 0; i < nch; i++) {
        decompress_chunk(&chunks[i]);
    }
    double end = omp_get_wtime();

    FILE *fout = fopen(argv[2], "wb");
    for (uint32_t i = 0; i < nch; i++) {
        fwrite(chunks[i].output, 1, chunks[i].orig_len, fout);
        free(chunks[i].output);
    }
    
    fclose(fout);
    free(buf);
    free(chunks);

    printf("Decompression complete in %.4fs\n", end - start);
    return 0;
}
