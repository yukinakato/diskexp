#define _GNU_SOURCE
#include "seq.h"
#include "drive.h"
#include "rng.h"
#include "tools.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct {
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	char *targetdrv;
	int tinterval;
	int curtemp;
} tempmon_t;

void init_seq_params(seq_params *p, char *drv, seq_rwmode mode, int tempmonitor_sec, char *logfilepath, int bufsize_MB, int calcsize) {
	p->targetdrv = drv;
	p->rwmode = mode;
	p->tempmonitor_sec = tempmonitor_sec;
	p->logfilepath = logfilepath;
	p->bufsize_MB = bufsize_MB;
	p->calcsize = calcsize;
	if (logfilepath != NULL)
		p->enablelogging = 1;
	else
		p->enablelogging = 0;
	if (tempmonitor_sec > 0)
		p->enabletempmonitoring = 1;
	else
		p->enabletempmonitoring = 0;
}

void *MonitorTemperature(void *p) {
	struct timespec t;
	tempmon_t *access = p;
	if (pthread_mutex_lock(&access->mutex) != 0) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "mutex lock failed");
		return NULL;
	}
	while (1) {
		if (getDriveTemp(access->targetdrv, &access->curtemp) != 0) {
			printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "getDriveTemp failed");
			return NULL;
		}
		clock_gettime(CLOCK_REALTIME, &t);
		t.tv_sec += access->tinterval; // set next update
		if (pthread_cond_timedwait(&access->cond, &access->mutex, &t) != ETIMEDOUT)
			break;
	}
	if (pthread_mutex_unlock(&access->mutex) != 0) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "mutex unlock failed");
		return NULL;
	}
	return NULL;
}

int SeqAccess(seq_params *params) {
	int fd;
	FILE *flog = NULL;
	uint64_t *wbuf, *rbuf;
	pcg32x2_random_t rng;
	uint64_t t, ptr, nsp, mst, calcstartpoint, c, physicalsectorsize, buf_MB;
	ssize_t retval;
	struct timespec tsa, tsb, tspa, tspb;
	pthread_t pth;
	tempmon_t tempmon;
	t = 0;
	physicalsectorsize = 0;
	wbuf = NULL;
	rbuf = NULL;

	if (CheckIfBlockDevice(params->targetdrv) != 1) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "target is not a block device");
		return -1;
	}
	if (getDriveSize(params->targetdrv, &t) != 0 || t < 1) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "getDriveSize failed");
		return -1;
	}
	if (getPhysicalSectorSize(params->targetdrv, &physicalsectorsize) != 0 || physicalsectorsize < 1) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "getPhysicalSectorSize failed");
		return -1;
	}
	if (t % physicalsectorsize != 0 || t == 0) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "target size is not a multiple of physical sector size or is zero");
		return -1;
	}

	// open target
	fd = open(params->targetdrv, O_RDWR | O_DIRECT);
	if (fd == -1) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "open target failed");
		return -1;
	}

	// prepare buffer
	buf_MB = params->bufsize_MB;
	if (buf_MB < 100) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "too small buffer size");
		return -1;
	}
	if (params->rwmode == seq_rwmode_w) {
		clock_gettime(CLOCK_MONOTONIC_RAW, &tsa);
		if (posix_memalign((void **)&wbuf, 1024 * 1024, 1024 * 1024 * buf_MB) != 0) {
			printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "memalign for wbuf failed");
			return -1;
		}
		pcg32x2_srandom_r(&rng, 42u, 42u, 54u, 54u);
		pcg32x2_srandom_r(&rng, time(NULL), time(NULL), (intptr_t)&rng, (intptr_t)&rng);
		for (ptr = 0; ptr < 1024 * 1024 * buf_MB / sizeof(uint64_t); ptr++) {
			wbuf[ptr] = pcg32x2_random_r(&rng);
		}
		clock_gettime(CLOCK_MONOTONIC_RAW, &tsb);
		printf("Preparation of Memory (Random %" PRIu64 " MB for write) - %" PRIu64 " ms\n", buf_MB, getDiffMS(tsa, tsb));
	} else if (params->rwmode == seq_rwmode_r) {
		if (posix_memalign((void **)&rbuf, 1024 * 1024, 1024 * 1024 * buf_MB) != 0) {
			printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "memalign for rbuf failed");
			return -1;
		}
		memset(rbuf, '\0', 1024 * 1024 * buf_MB);
	} else {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "undefined access mode");
		return -1;
	}

	// if logging enabled
	if (params->enablelogging) {
		flog = fopen(params->logfilepath, "w");
		if (flog == NULL) {
			printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "fopen failed");
			return -1;
		}
	}

	// if temp monitoring enabled
	if (params->enabletempmonitoring) {
		puts("temp monitor enabled!!");
		tempmon.targetdrv = params->targetdrv;
		tempmon.tinterval = params->tempmonitor_sec;
		if (getDriveTemp(tempmon.targetdrv, &tempmon.curtemp) != 0) {
			printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "getDriveTemp failed");
		}
		// create another thread for temperature monitoring, mutex lock required when accessing tempmon
		pthread_mutex_init(&tempmon.mutex, NULL);
		pthread_cond_init(&tempmon.cond, NULL);
		if (pthread_create(&pth, NULL, MonitorTemperature, &tempmon) != 0) {
			printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "create thread failed");
		}
		sleep(1);
	} else {
		tempmon.curtemp = -99;
	}

	// fire!
	puts("Start Seq Access...");
	printf("StartPos\tPos[%%]\tBytesR/W\tSpeed[MB/s]\tTime[msec]\tTemperature[C]\n");
	if (params->enablelogging) {
		fprintf(flog, "#StartPos\tPos[%%]\tBytesR/W\tSpeed[MB/s]\tTime[msec]\tTemperature[C]\n");
	}
	ptr = 0;
	calcstartpoint = 0;
	nsp = 0;
	clock_gettime(CLOCK_MONOTONIC_RAW, &tsa);
	for (c = 0; c < t;) {
		retval = 0;
		clock_gettime(CLOCK_MONOTONIC_RAW, &tspa);
		if (params->rwmode == seq_rwmode_w) {
			retval = write(fd, &wbuf[ptr], 1024 * 1024);
		} else if (params->rwmode == seq_rwmode_r) {
			retval = read(fd, &rbuf[ptr], 1024 * 1024);
		}
		clock_gettime(CLOCK_MONOTONIC_RAW, &tspb);
		nsp += getDiffNS(tspa, tspb);
		if (retval == -1 || retval == 0) {
			printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "read/write error");
			return -1;
		}
		ptr += retval / sizeof(uint64_t);
		if (ptr == 1024 * 1024 * buf_MB / sizeof(uint64_t))
			ptr = 0;

		c += retval;
		if (c / 1024 / 1024 % params->calcsize == 0 || c == t) {
			printf("%" PRIu64 "\t%.4f\t%" PRIu64 "\t%.2f\t%" PRIu64 "\t%d\n", calcstartpoint, (double)calcstartpoint / t * 100,
				   c - calcstartpoint, (double)(c - calcstartpoint) * 1000 / nsp, nsp / 1000 / 1000, atomic_load(&tempmon.curtemp));
			if (params->enablelogging) {
				fprintf(flog, "%" PRIu64 "\t%.4f\t%" PRIu64 "\t%.2f\t%" PRIu64 "\t%d\n", calcstartpoint, (double)calcstartpoint / t * 100,
						c - calcstartpoint, (double)(c - calcstartpoint) * 1000 / nsp, nsp / 1000 / 1000, atomic_load(&tempmon.curtemp));
			}
			calcstartpoint = c;
			nsp = 0;
		}
	}
	clock_gettime(CLOCK_MONOTONIC_RAW, &tsb);
	mst = getDiffMS(tsa, tsb);

	// stop temp monitoring thread
	if (params->enabletempmonitoring) {
		if (pthread_mutex_lock(&tempmon.mutex) != 0) {
			printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "mutex lock failed");
			return -1;
		}
		if (pthread_cond_signal(&tempmon.cond) != 0) {
			printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "signaling failed");
			return -1;
		}
		if (pthread_mutex_unlock(&tempmon.mutex) != 0) {
			printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "mutex unlock failed");
			return -1;
		}
		if (pthread_join(pth, NULL) != 0) {
			printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "pthread join failed");
			return -1;
		}
	}

	printf("Target               = %s\n", params->targetdrv);
	printf("Target Device Size   = %" PRIu64 "\n", t);
	printf("Total RW Bytes       = %" PRIu64 "\n", c);
	printf("Elapsed Time         = %d h %d m %d s\n", getHMSfromMS(mst).h, getHMSfromMS(mst).m, getHMSfromMS(mst).s);
	printf("Average Throughput   = %.2f [MB/s]\n", (double)t / mst / 1000);

	// finalize
	if (params->rwmode == seq_rwmode_r)
		if (rbuf != NULL)
			free(rbuf);
	if (params->rwmode == seq_rwmode_w)
		if (wbuf != NULL)
			free(wbuf);

	if (params->enablelogging) {
		if (fclose(flog) != 0) {
			printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "fclose failed");
			return -1;
		}
	}
	if (close(fd) == -1) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "close target failed");
		return -1;
	}
	return 0;
}
