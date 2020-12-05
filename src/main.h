#pragma once

typedef enum { // avoid one-line formatting
	opmode_verify,
	opmode_susrandom,
	opmode_seq,
	opmode_refresh,
	opmode_undefined
} opmode;

typedef struct {
	opmode op;
	void *params;
} op_params;
