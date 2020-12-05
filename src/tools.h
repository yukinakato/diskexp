#pragma once

#include <stdint.h>
#include <time.h>

typedef struct {
	int h;
	int m;
	int s;
} hms;

hms getHMSfromMS(uint64_t ms);
uint64_t getDiffMS(struct timespec start, struct timespec end);
uint64_t getDiffNS(struct timespec start, struct timespec end);
