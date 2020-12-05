#include "rng.h"

uint32_t pcg32_random_r(pcg32_random_t *rng) {
	uint64_t oldstate = rng->state;
	rng->state = oldstate * 6364136223846793005ULL + rng->inc;
	uint32_t xorshifted = ((oldstate >> 18u) ^ oldstate) >> 27u;
	uint32_t rot = oldstate >> 59u;
	return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

uint32_t pcg32_boundedrand_r(pcg32_random_t *rng, uint32_t bound) {
	uint32_t r;
	uint32_t threshold = -bound % bound;
	for (;;) {
		r = pcg32_random_r(rng);
		if (r >= threshold)
			break;
	}
	return r % bound;
}

void pcg32_srandom_r(pcg32_random_t *rng, uint64_t initstate, uint64_t initseq) {
	rng->state = 0U;
	rng->inc = (initseq << 1u) | 1u;
	pcg32_random_r(rng);
	rng->state += initstate;
	pcg32_random_r(rng);
}

void pcg32x2_srandom_r(pcg32x2_random_t *rng, uint64_t seed1, uint64_t seed2, uint64_t seq1, uint64_t seq2) {
	uint64_t mask = ~0ull >> 1;
	if ((seq1 & mask) == (seq2 & mask))
		seq2 = ~seq2;
	pcg32_srandom_r(rng->gen, seed1, seq1);
	pcg32_srandom_r(rng->gen + 1, seed2, seq2);
}

uint64_t pcg32x2_random_r(pcg32x2_random_t *rng) { return ((uint64_t)(pcg32_random_r(rng->gen)) << 32) | pcg32_random_r(rng->gen + 1); }

uint64_t pcg32x2_boundedrand_r(pcg32x2_random_t *rng, uint64_t bound) {
	uint64_t r;
	uint64_t threshold = -bound % bound;
	for (;;) {
		r = pcg32x2_random_r(rng);
		if (r >= threshold)
			break;
	}
	return r % bound;
}
