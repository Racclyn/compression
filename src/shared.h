#ifndef SHARED_H
#define SHARED_H

#include <stdint.h>
#include <stdlib.h>

#define TOP_VALUE      0xFFFFFFFFU
#define HALF           0x80000000U
#define FIRST_QUARTER  0x40000000U
#define THIRD_QUARTER  0xC0000000U
#define MAX_COUNT      16384
#define ALPHABET_MAX   258

typedef struct {
    uint32_t counts[ALPHABET_MAX];
    uint32_t cumulative[ALPHABET_MAX + 1];
    uint32_t total;
} AdaptiveModel;

static inline void model_init(AdaptiveModel *m) {
    m->total = ALPHABET_MAX;
    for (int i = 0; i < ALPHABET_MAX; i++) {
        m->counts[i] = 1;
        m->cumulative[i] = i;
    }
    m->cumulative[ALPHABET_MAX] = ALPHABET_MAX;
}

static inline void model_update(AdaptiveModel *m, int symbol) {
    m->counts[symbol]++;
    m->total++;
    if (m->total < MAX_COUNT) {
        for (int i = symbol + 1; i <= ALPHABET_MAX; i++) m->cumulative[i]++;
        return;
    }
    uint32_t sum = 0;
    for (int i = 0; i < ALPHABET_MAX; i++) {
        m->counts[i] = (m->counts[i] >> 1) | 1;
        m->cumulative[i] = sum;
        sum += m->counts[i];
    }
    m->total = sum;
    m->cumulative[ALPHABET_MAX] = sum;
}

typedef struct {
    uint8_t *data;
    uint32_t bit_ptr, low, high, pending;
} AC_Encoder;

typedef struct {
    const uint8_t *data;
    uint32_t bit_ptr, bit_limit, low, high, value;
} AC_Decoder;

static inline void ac_encode_symbol(AC_Encoder *e, AdaptiveModel *m, int sym) {
    uint64_t range = (uint64_t)e->high - e->low + 1;
    e->high = e->low + (uint32_t)((range * m->cumulative[sym + 1]) / m->total) - 1;
    e->low = e->low + (uint32_t)((range * m->cumulative[sym]) / m->total);
    while (1) {
        if (e->high < HALF) {
            e->bit_ptr++;
            while (e->pending--) { e->data[e->bit_ptr >> 3] |= (128 >> (e->bit_ptr & 7)); e->bit_ptr++; }
            e->pending = 0;
            goto next;
        }
        if (e->low >= HALF) {
            e->data[e->bit_ptr >> 3] |= (128 >> (e->bit_ptr & 7)); e->bit_ptr++;
            while (e->pending--) { e->bit_ptr++; }
            e->pending = 0; e->low -= HALF; e->high -= HALF;
            goto next;
        }
        if (e->low >= FIRST_QUARTER && e->high < THIRD_QUARTER) {
            e->pending++; e->low -= FIRST_QUARTER; e->high -= FIRST_QUARTER;
            goto next;
        }
        return;
        next:
        e->low <<= 1; e->high = (e->high << 1) | 1;
    }
}

static inline int ac_decode_symbol(AC_Decoder *d, AdaptiveModel *m) {
    uint64_t range = (uint64_t)d->high - d->low + 1;
    uint32_t target = (uint32_t)((((uint64_t)d->value - d->low + 1) * m->total - 1) / range);
    int sym = 0;
    while (m->cumulative[sym + 1] <= target) sym++;
    d->high = d->low + (uint32_t)((range * m->cumulative[sym + 1]) / m->total) - 1;
    d->low = d->low + (uint32_t)((range * m->cumulative[sym]) / m->total);
    while (1) {
        if (d->high < HALF) goto shift;
        if (d->low >= HALF) { d->value -= HALF; d->low -= HALF; d->high -= HALF; goto shift; }
        if (d->low >= FIRST_QUARTER && d->high < THIRD_QUARTER) { d->value -= FIRST_QUARTER; d->low -= FIRST_QUARTER; d->high -= FIRST_QUARTER; goto shift; }
        return sym;
        shift:
        d->low <<= 1; d->high = (d->high << 1) | 1;
        int bit = (d->bit_ptr < d->bit_limit) ? (d->data[d->bit_ptr >> 3] >> (7 - (d->bit_ptr & 7))) & 1 : 0;
        d->value = (d->value << 1) | bit; d->bit_ptr++;
    }
}

#endif
