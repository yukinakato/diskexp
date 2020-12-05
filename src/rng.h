#pragma once

#include <stdint.h>

typedef struct {
	uint64_t state;
	uint64_t inc;
} pcg32_random_t;

typedef struct {
	pcg32_random_t gen[2];
} pcg32x2_random_t;

uint32_t pcg32_random_r(pcg32_random_t *rng);
uint32_t pcg32_boundedrand_r(pcg32_random_t *rng, uint32_t bound);
void pcg32_srandom_r(pcg32_random_t *rng, uint64_t initstate, uint64_t initseq);
void pcg32x2_srandom_r(pcg32x2_random_t *rng, uint64_t seed1, uint64_t seed2, uint64_t seq1, uint64_t seq2);
uint64_t pcg32x2_random_r(pcg32x2_random_t *rng);
uint64_t pcg32x2_boundedrand_r(pcg32x2_random_t *rng, uint64_t bound);
