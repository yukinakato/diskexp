#pragma once

typedef enum { //
	seq_rwmode_r,
	seq_rwmode_w,
	seq_rwmode_undefined
} seq_rwmode;

typedef struct {
	char *targetdrv;
	seq_rwmode rwmode;
	int bufsize_MB;
	int calcsize;
	int enablelogging;
	char *logfilepath;
	int enabletempmonitoring;
	int tempmonitor_sec;
} seq_params;

void init_seq_params(seq_params *params, char *targetdrv, seq_rwmode mode, int tempmonitor_sec, char *logfilepath, int bufsize_MB,
					 int calcsize);
int SeqAccess(seq_params *params);
