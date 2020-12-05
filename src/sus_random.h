#pragma once

typedef enum { //
	susr_rwmode_r,
	susr_rwmode_w,
	susr_rwmode_rw,
	susr_rwmode_undefined
} susrandom_rwmode;

typedef struct {
	char *targetdrv;
	susrandom_rwmode rwmode;
	int iosize;
	int durationsec;
	int enablelogging;
	char *logfilepath;
} susrandom_params;

void init_susrandom_params(susrandom_params *params, char *targetdrv, susrandom_rwmode mode, int iosize, int duration, char *logfilepath);
int SustainedRandomAccess(susrandom_params *params);
