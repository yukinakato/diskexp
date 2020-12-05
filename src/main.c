#define _GNU_SOURCE
#include "main.h"
#include "refresh.h"
#include "seq.h"
#include "sus_random.h"
#include "verify.h"
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#include <pthread.h>
//#include <sched.h>

void PrintUsage(void) {
	puts("usage:");
	puts("diskexp --verify device");
	puts("diskexp --susrandom {r|w|rw} [-b 4096] [-t 300] [-o log.txt] device");
	puts("    where  --susrandom rwmode");
	puts("           -b blocksize_in_byte (default 4096)");
	puts("           -t duration_in_sec (default 10)");
	puts("           -o logfile");
	puts("diskexp --seq {r|w} [--calcsize 500] [-o log.txt] [--tempmonitor 30] device");
	puts("    where  --seq rwmode");
	puts("           --calcsize calc_every_MiB (default 500)");
	puts("           -o logfile");
	puts("           --tempmonitor interval_in_sec");
	puts("diskexp --refresh [--safe] device");
}

int ParseOption(int argc, char *argv[], op_params *work) {
	// DON'T USE OPTIONAL ARGUMENT
	struct option longopts[] = {{"verify", no_argument, NULL, 'v'}, // avoid auto-formatting
								{"susrandom", required_argument, NULL, 'r'},
								{"seq", required_argument, NULL, 's'},
								{"refresh", no_argument, NULL, 'f'},
								{"calcsize", required_argument, NULL, 'c'},
								{"tempmonitor", required_argument, NULL, 'm'},
								{"safe", no_argument, NULL, 'x'},
								{0, 0, 0, 0}};
	int val;
	int opt_tempmonitorinterval = -1;
	int opt_blocksize = -1;
	int opt_calcsize = -1;
	int opt_duration = -1;
	int opt_safemode = 0;
	char *opt_o = NULL;
	char *opt_device = NULL;
	opmode opt_opmode = opmode_undefined;
	seq_rwmode opt_seq_rwmode = seq_rwmode_undefined;
	susrandom_rwmode opt_susr_rwmode = susr_rwmode_undefined;

	if (argc < 3 || argc > 10) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "invalid number of arguments");
		return -1;
	}

	// opterr = 0; // suppress getopt error message
	while ((val = getopt_long(argc, argv, "b:t:o:", longopts, NULL)) != -1) {
		switch (val) { // check if defined previously
			case 'v':
			case 'r':
			case 's':
			case 'f':
				if (opt_opmode != opmode_undefined) {
					printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "opmode should be defined only once");
					return -1;
				}
				break;
			case 'm':
				if (opt_tempmonitorinterval != -1) {
					printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "--tempmonitor should be defined only once");
					return -1;
				}
				break;
			case 'b':
				if (opt_blocksize != -1) {
					printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "-b should be defined only once");
					return -1;
				}
				break;
			case 'c':
				if (opt_calcsize != -1) {
					printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "--calcsize should be defined only once");
					return -1;
				}
				break;
			case 't':
				if (opt_duration != -1) {
					printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "-t should be defined only once");
					return -1;
				}
				break;
			case 'o':
				if (opt_o != NULL) {
					printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "-o should be defined only once");
					return -1;
				}
				break;
			case 'x':
				if (opt_safemode == 1) {
					printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "--safe should be defined only once");
					return -1;
				}
				break;
		}
		switch (val) {
			case 'v':
				opt_opmode = opmode_verify;
				break;
			case 'r':
				opt_opmode = opmode_susrandom;
				if (optarg != 0) {
					if (strcmp("r", optarg) == 0) {
						opt_susr_rwmode = susr_rwmode_r;
					} else if (strcmp("w", optarg) == 0) {
						opt_susr_rwmode = susr_rwmode_w;
					} else if (strcmp("rw", optarg) == 0) {
						opt_susr_rwmode = susr_rwmode_rw;
					} else {
						printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "rw mode doesn't match r|w|rw");
						return -1;
					}
				}
				break;
			case 's':
				opt_opmode = opmode_seq;
				if (optarg != 0) {
					if (strcmp("r", optarg) == 0) {
						opt_seq_rwmode = seq_rwmode_r;
					} else if (strcmp("w", optarg) == 0) {
						opt_seq_rwmode = seq_rwmode_w;
					} else {
						printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "rw mode doesn't match r|w");
						return -1;
					}
				}
				break;
			case 'f':
				opt_opmode = opmode_refresh;
				break;
			case 'm':
				opt_tempmonitorinterval = atoi(optarg);
				if (opt_tempmonitorinterval <= 0) {
					printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "temp monitor interval can't be <= 0 or atoi failed");
					return -1;
				}
				break;
			case 'b':
				opt_blocksize = atoi(optarg);
				if (opt_blocksize <= 0) {
					printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "-b (block size) can't be <= 0 or atoi failed");
					return -1;
				}
				break;
			case 'c':
				opt_calcsize = atoi(optarg);
				if (opt_calcsize <= 0) {
					printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "calcsize can't be <= 0 or atoi failed");
					return -1;
				}
				break;
			case 't':
				opt_duration = atoi(optarg);
				if (opt_duration <= 0) {
					printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "-t (duration) can't be <= 0 or atoi failed");
					return -1;
				}
				break;
			case 'x':
				opt_safemode = 1;
				break;
			case 'o':
				if (strlen(optarg) < 1) {
					printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "path contains nothing");
					return -1;
				}
				opt_o = malloc(sizeof(char) * (strlen(optarg) + 1));
				if (opt_o == NULL) {
					printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "malloc failed");
					return -1;
				}
				strcpy(opt_o, optarg);
				break;
			default:
				printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "unrecognized option");
				return -1;
		}
	}

	if ((argc - optind) != 1) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "wrong number of optind");
		return -1;
	}

	opt_device = argv[optind];

	// assignment
	work->op = opt_opmode;
	switch (opt_opmode) {
		case opmode_verify:
			work->params = malloc(sizeof(verify_params));
			break;
		case opmode_susrandom:
			work->params = malloc(sizeof(susrandom_params));
			break;
		case opmode_seq:
			work->params = malloc(sizeof(seq_params));
			break;
		case opmode_refresh:
			work->params = malloc(sizeof(refresh_params));
			break;
		default:
			printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "opmode undefined");
			return -1;
	}
	if (work->params == NULL) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "work malloc failed");
		return -1;
	}

	switch (opt_opmode) {
		case opmode_verify:
			work->params = malloc(sizeof(verify_params));
			init_verify_params((verify_params *)work->params, opt_device, 512);
			break;
		case opmode_susrandom:
			work->params = malloc(sizeof(susrandom_params));
			if (opt_blocksize == -1)
				opt_blocksize = 4096;
			if (opt_duration == -1)
				opt_duration = 10;
			init_susrandom_params(work->params, opt_device, opt_susr_rwmode, opt_blocksize, opt_duration, opt_o);
			break;
		case opmode_seq:
			work->params = malloc(sizeof(seq_params));
			if (opt_calcsize == -1)
				opt_calcsize = 500;
			init_seq_params(work->params, opt_device, opt_seq_rwmode, opt_tempmonitorinterval, opt_o, 512, opt_calcsize);
			break;
		case opmode_refresh:
			work->params = malloc(sizeof(refresh_params));
			init_refresh_params(work->params, opt_device, 512, opt_safemode);
			break;
		default:
			printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "opmode undefined");
			break;
	}

	return 0;
}

int main(int argc, char *argv[]) {
	int ret = 0;
	op_params work = {opmode_undefined, NULL};

	// cpu_set_t cpuset;
	// CPU_ZERO(&cpuset);
	// CPU_SET(0, &cpuset);
	// pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

	setbuf(stdout, NULL);

	if (ParseOption(argc, argv, &work) != 0) {
		puts("");
		PrintUsage();
		return -1;
	}

	switch (work.op) {
		case opmode_verify:
			ret = VerifyDisk((verify_params *)work.params);
			break;
		case opmode_susrandom:
			ret = SustainedRandomAccess((susrandom_params *)work.params);
			break;
		case opmode_seq:
			ret = SeqAccess((seq_params *)work.params);
			break;
		case opmode_refresh:
			ret = RefreshDisk((refresh_params *)work.params);
			break;
		case opmode_undefined:
			printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "opmode undefined");
			return -1;
	}

	if (ret != 0)
		puts("operation failed");
	else
		puts("operation finished successfully");

	return ret;
}
