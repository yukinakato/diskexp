#include "tools.h"
#include <stdint.h>
#include <time.h>

uint64_t getDiffMS(struct timespec start, struct timespec end) {
	return (uint64_t)(end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000 / 1000;
}

uint64_t getDiffNS(struct timespec start, struct timespec end) {
	return (uint64_t)(end.tv_sec - start.tv_sec) * 1000 * 1000 * 1000 + end.tv_nsec - start.tv_nsec;
}

hms getHMSfromMS(uint64_t ms) {
	hms v;
	v.h = (int)(ms / 1000 / 3600);
	v.m = (int)((ms / 1000 % 3600) / 60);
	v.s = (int)((ms / 1000 % 3600) % 60);
	return v;
}
