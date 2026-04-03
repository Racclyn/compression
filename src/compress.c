#include <stdio.h>
#include <string.h>
#include <omp.h>
#include "shared.h"

#define CHUNK_SIZE 500000

typedef struct {
    uint32_t orig_len, rle_len, bit_len, bwt_idx;
    uint8_t *payload;
} Chunk;

static __thread int *suffix_rank;
static __thread int  suffix_step, suffix_len;

static int cmp_suffixes(const void *a, const void *b) {
    int i = *(int*)a, j = *(int*)b;
    if (suffix_rank[i] != suffix_rank[j]) return suffix_rank[i] - suffix_rank[j];
    int ri = (i + suffix_step < suffix_len) ? suffix_rank[i + suffix_step] : -1;
    int rj = (j + suffix_step < suffix_len) ? suffix_rank[j + suffix_step] : -1;
    return ri - rj;
}

void compress_chunk(const uint8_t *src, int n, Chunk *out) {
    int blen = n + 1;
    uint8_t *bin = malloc(blen); memcpy(bin, src, n); bin[n] = 0;
    int *sa = malloc(blen * sizeof(int)), *tmp = malloc(blen * sizeof(int));
    suffix_rank = malloc(blen * sizeof(int)); suffix_len = blen;
    for (int i = 0; i < blen; i++) { sa[i] = i; suffix_rank[i] = bin[i]; }
    for (suffix_step = 1; suffix_step < blen; suffix_step <<= 1) {
        qsort(sa, blen, sizeof(int), cmp_suffixes);
        tmp[sa[0]] = 0;
        for (int i = 1; i < blen; i++) tmp[sa[i]] = tmp[sa[i-1]] + (cmp_suffixes(&sa[i-1], &sa[i]) < 0);
        memcpy(suffix_rank, tmp, blen * sizeof(int));
    }
    uint8_t *bout = malloc(blen);
    for (int i = 0; i < blen; i++) {
        if (sa[i] == 0) out->bwt_idx = i;
        bout[i] = (sa[i] == 0) ? bin[blen - 1] : bin[sa[i] - 1];
    }
    free(bin); free(sa); free(tmp); free(suffix_rank);

    uint8_t alpha[256]; for (int i = 0; i < 256; i++) alpha[i] = i;
    uint16_t *rle = malloc(blen * sizeof(uint16_t));
    int rp = 0, zrun = 0;
    for (int i = 0; i < blen; i++) {
        int pos = 0; while (alpha[pos] != bout[i]) pos++;
        if (pos == 0) { zrun++; continue; }
        while (zrun > 0) { rle[rp++] = (zrun % 2 == 1) ? 0 : 1; zrun = (zrun - (rle[rp-1] + 1)) / 2; }
        rle[rp++] = pos + 1;
        memmove(alpha + 1, alpha, pos); alpha[0] = bout[i];
    }
    while (zrun > 0) { rle[rp++] = (zrun % 2 == 1) ? 0 : 1; zrun = (zrun - (rle[rp-1] + 1)) / 2; }
    free(bout);

    AdaptiveModel model; model_init(&model);
    AC_Encoder enc = { .data = calloc(rp * 2 + 8, 1), .bit_ptr = 0, .low = 0, .high = TOP_VALUE, .pending = 0 };
    for (int i = 0; i < rp; i++) {
        ac_encode_symbol(&enc, &model, rle[i]);
        model_update(&model, rle[i]);
    }
    if (enc.low >= FIRST_QUARTER) enc.data[enc.bit_ptr >> 3] |= (128 >> (enc.bit_ptr & 7));
    enc.bit_ptr++; enc.pending++;
    while (enc.pending--) { if (enc.low < FIRST_QUARTER) enc.data[enc.bit_ptr >> 3] |= (128 >> (enc.bit_ptr & 7)); enc.bit_ptr++; }
    out->orig_len = n; out->rle_len = rp; out->bit_len = enc.bit_ptr; out->payload = enc.data;
    free(rle);
}

int main(int argc, char **argv) {
    if (argc < 3) { printf("Usage: %s <input> <output>\n", argv[0]); return 1; }
    FILE *fin = fopen(argv[1], "rb");
    if (!fin) return 1;
    fseek(fin, 0, SEEK_END); long fsize = ftell(fin); rewind(fin);
    uint8_t *in = malloc(fsize); fread(in, 1, fsize, fin); fclose(fin);
    int nch = (fsize + CHUNK_SIZE - 1) / CHUNK_SIZE;
    Chunk *res = calloc(nch, sizeof(Chunk));
    double start = omp_get_wtime();
    #pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < nch; i++) {
        int off = i * CHUNK_SIZE;
        int n = (off + CHUNK_SIZE > fsize) ? (fsize - off) : CHUNK_SIZE;
        compress_chunk(in + off, n, &res[i]);
    }
    double end = omp_get_wtime();
    FILE *fout = fopen(argv[2], "wb");
    size_t out_bytes = 0;
    for (int i = 0; i < nch; i++) {
        uint32_t h[4] = {res[i].orig_len, res[i].rle_len, res[i].bit_len, res[i].bwt_idx};
        fwrite(h, 4, 4, fout);
        size_t p_size = (res[i].bit_len + 7) / 8;
        fwrite(res[i].payload, 1, p_size, fout);
        out_bytes += 16 + p_size;
        free(res[i].payload);
    }
    fclose(fout);
    printf("Input: %ld bytes | Output: %zu bytes\n", fsize, out_bytes);
    printf("Ratio: %.2f%% | Time: %.4fs\n", (1.0 - (double)out_bytes / fsize) * 100, end - start);
    free(in); free(res);
    return 0;
}
