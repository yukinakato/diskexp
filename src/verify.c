#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE
#include "verify.h"
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
	uint64_t current;
	uint64_t total;
} progression;

void init_verify_params(verify_params *p, char *drv, int bufsize_MB) {
	p->targetdrv = drv;
	p->bufsize_MB = bufsize_MB;
}

void *PrintVerifyProgression(void *p) {
	struct timespec t;
	progression *prog = p;
	if (pthread_mutex_lock(&prog->mutex) != 0) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "mutex lock failed");
		return NULL;
	}
	while (1) {
		printf("\r%.2f %% Completed", (double)prog->current / prog->total * 100);
		clock_gettime(CLOCK_REALTIME, &t);
		t.tv_sec++; // update every seconds
		if (pthread_cond_timedwait(&prog->cond, &prog->mutex, &t) != ETIMEDOUT)
			break;
	}
	// should be 100 % completed
	if (prog->current != prog->total)
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "total and current are different");
	printf("\r%.2f %% completed\n", (double)prog->current / prog->total * 100);
	if (pthread_mutex_unlock(&prog->mutex) != 0) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "mutex unlock failed");
	}
	return NULL;
}

int VerifyDisk(verify_params *params) {
	int fd;
	uint64_t *wbuf, *rbuf;
	pcg32x2_random_t rng;
	uint64_t c, t, ptr, ptrtmp, tcomp, ms, numdiffers, physicalsectorsize, buf_MB;
	ssize_t retval;
	struct timespec tsa, tsb;
	pthread_t pth;
	progression prog;
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
	prog.current = 0;
	prog.total = t;

	// open target
	fd = open(params->targetdrv, O_RDWR | O_DIRECT);
	if (fd == -1) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "open target failed");
		return -1;
	}

	// prepare buffer and random data to wbuf
	buf_MB = params->bufsize_MB;
	if (buf_MB < 100) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "too small buffer size");
		return -1;
	}
	clock_gettime(CLOCK_MONOTONIC_RAW, &tsa);
	if (posix_memalign((void **)&wbuf, 1024 * 1024, 1024 * 1024 * buf_MB) != 0) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "memalign for wbuf failed");
		return -1;
	}
	if (posix_memalign((void **)&rbuf, 1024 * 1024, 1024 * 1024 * buf_MB) != 0) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "memalign for rbuf failed");
		return -1;
	}
	pcg32x2_srandom_r(&rng, 42u, 42u, 54u, 54u);
	pcg32x2_srandom_r(&rng, time(NULL), time(NULL), (intptr_t)&rng, (intptr_t)&rng);
	for (ptr = 0; ptr < 1024 * 1024 * buf_MB / sizeof(uint64_t); ptr++) {
		wbuf[ptr] = pcg32x2_random_r(&rng);
	}
	memset(rbuf, '\0', 1024 * 1024 * buf_MB);
	clock_gettime(CLOCK_MONOTONIC_RAW, &tsb);
	printf("Preparation of Memory (%" PRIu64 " MB) - %" PRIu64 " ms\n", buf_MB, getDiffMS(tsa, tsb));

	// create another thread for write progression monitoring, mutex lock required when accessing prog
	puts("Start Writing...");
	pthread_mutex_init(&prog.mutex, NULL);
	pthread_cond_init(&prog.cond, NULL);
	if (pthread_create(&pth, NULL, PrintVerifyProgression, &prog) != 0) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "create thread failed");
	}

	// write!
	clock_gettime(CLOCK_MONOTONIC_RAW, &tsa);
	ptr = 0;
	for (c = 0; c < t;) {
		retval = write(fd, &wbuf[ptr], 1024 * 1024);
		if (retval == -1 || retval == 0) {
			printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "write error");
			return -1;
		}
		atomic_fetch_add(&prog.current, retval);
		c += retval;
		ptr += retval / sizeof(uint64_t);
		if (ptr == 1024 * 1024 * buf_MB / sizeof(uint64_t))
			ptr = 0;
	}

	// write finished, stop progression monitoring thread
	clock_gettime(CLOCK_MONOTONIC_RAW, &tsb);
	if (pthread_mutex_lock(&prog.mutex) != 0) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "mutex lock failed");
		return -1;
	}
	if (pthread_cond_signal(&prog.cond) != 0) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "signaling failed");
		return -1;
	}
	if (pthread_mutex_unlock(&prog.mutex) != 0) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "mutex unlock failed");
		return -1;
	}
	if (pthread_join(pth, NULL) != 0) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "pthread join failed");
		return -1;
	}

	// show statistical result
	ms = getDiffMS(tsa, tsb);
	printf("Write Operation  - %" PRIu64 " ms (%d h %d m %d s)\n", ms, getHMSfromMS(ms).h, getHMSfromMS(ms).m, getHMSfromMS(ms).s);
	printf("Write Throughput - %.2f MB/s\n", (double)t / ms / 1000);

	// wait...
	puts("Sleep for 30 seconds...");
	sleep(30);

	// read & compare!
	puts("Start Reading...");
	prog.current = 0;
	tcomp = 0;
	numdiffers = 0;
	if (lseek64(fd, 0, SEEK_SET) == -1) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "lseek to 0 failed");
		return -1;
	}

	// create another thread for read progression monitoring, mutex lock required when accessing prog
	if (pthread_create(&pth, NULL, PrintVerifyProgression, &prog) != 0) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "create thread failed");
	}

	clock_gettime(CLOCK_MONOTONIC_RAW, &tsa);
	ptr = 0;
	for (c = 0; c < t;) {
		retval = read(fd, &rbuf[ptr], 1024 * 1024);
		if (retval == -1 || retval == 0) {
			printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "read error");
			return -1;
		}
		atomic_fetch_add(&prog.current, retval);
		c += retval;
		ptr += retval / sizeof(uint64_t);
		if (ptr == 1024 * 1024 * buf_MB / sizeof(uint64_t) || c == t) {
			ptrtmp = ptr;
			for (ptr = 0; ptr < ptrtmp; ptr += physicalsectorsize / sizeof(uint64_t)) {
				if (memcmp(&wbuf[ptr], &rbuf[ptr], physicalsectorsize) != 0) {
					printf("\n*** Differ at Position %" PRIu64 " (sector # %" PRIu64 ")\n", tcomp, tcomp / physicalsectorsize);
					numdiffers++;
				}
				tcomp += physicalsectorsize;
			}
			ptr = 0;
		}
	}

	// read finished, stop progression monitoring thread
	clock_gettime(CLOCK_MONOTONIC_RAW, &tsb);
	if (pthread_mutex_lock(&prog.mutex) != 0) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "mutex lock failed");
		return -1;
	}
	if (pthread_cond_signal(&prog.cond) != 0) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "signaling failed");
		return -1;
	}
	if (pthread_mutex_unlock(&prog.mutex) != 0) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "mutex unlock failed");
		return -1;
	}
	if (pthread_join(pth, NULL) != 0) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "pthread join failed");
		return -1;
	}

	// show statistical result
	ms = getDiffMS(tsa, tsb);
	printf("Read  Operation  - %" PRIu64 " ms (%d h %d m %d s)\n", ms, getHMSfromMS(ms).h, getHMSfromMS(ms).m, getHMSfromMS(ms).s);
	printf("Read  Throughput - %.2f MB/s\n", (double)t / ms / 1000);
	printf("Target               = %s\n", params->targetdrv);
	printf("Target Device Size   = %" PRIu64 "\n", t);
	printf("Total Compared Bytes = %" PRIu64 "\n", tcomp);
	if (numdiffers > 0)
		printf("*** %" PRIu64 " Differ Detected! ***\n", numdiffers);
	else
		printf("*** No Differ Detected ***\n");

	// finalize
	if (wbuf != NULL)
		free(wbuf);
	if (rbuf != NULL)
		free(rbuf);
	if (close(fd) == -1) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "close target failed");
		return -1;
	}
	return 0;
}
