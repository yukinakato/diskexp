#pragma once

typedef struct {
	char *targetdrv;
	int bufsize_MB;
} verify_params;

void init_verify_params(verify_params *params, char *targetdrv, int bufsize_MB);
int VerifyDisk(verify_params *params);
