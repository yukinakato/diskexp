#pragma once

typedef struct {
	char *targetdrv;
	int bufsize_MB;
	int verify;
} refresh_params;

void init_refresh_params(refresh_params *params, char *targetdrv, int bufsize_MB, int enableverify);
int RefreshDisk(refresh_params *params);
