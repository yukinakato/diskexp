#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE
#include "refresh.h"
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

void init_refresh_params(refresh_params *p, char *drv, int bufsize_MB, int enableverify) {
	p->targetdrv = drv;
	p->bufsize_MB = bufsize_MB;
	p->verify = enableverify;
}

void *PrintRefreshProgression(void *p) {
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
	if (prog->total != prog->current)
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "total and current are different");
	printf("\r%.2f %% completed\n", (double)prog->current / prog->total * 100);
	if (pthread_mutex_unlock(&prog->mutex) != 0) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "mutex unlock failed");
		return NULL;
	}
	return NULL;
}

int RefreshDisk(refresh_params *params) {
	int fd;
	uint64_t *buf, *vtbuf;
	uint64_t t, ptr, ptrtmp, loopstart, c, physicalsectorsize, buf_MB;
	ssize_t retval;
	pthread_t pth;
	progression prog;
	t = 0;
	physicalsectorsize = 0;
	buf = NULL;
	vtbuf = NULL;

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

	// prepare buffer
	buf_MB = params->bufsize_MB;
	if (buf_MB < 100) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "too small buffer size");
		return -1;
	}
	if (posix_memalign((void **)&buf, 1024 * 1024, 1024 * 1024 * buf_MB) != 0) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "memalign for buf failed");
		return -1;
	}
	memset(buf, '\0', 1024 * 1024 * buf_MB);
	if (params->verify) {
		if (posix_memalign((void **)&vtbuf, 1024 * 1024, 1024 * 1024 * buf_MB) != 0) {
			printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "memalign for vtbuf failed");
			return -1;
		}
		memset(vtbuf, '\0', 1024 * 1024 * buf_MB);
	}

	// create another thread for write progression monitoring, mutex lock required when accessing prog
	puts("Start Refresh...");
	pthread_mutex_init(&prog.mutex, NULL);
	pthread_cond_init(&prog.cond, NULL);
	if (pthread_create(&pth, NULL, PrintRefreshProgression, &prog) != 0) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "create thread failed");
	}

	// fire!
	ptr = 0;
	loopstart = 0;
	for (c = 0; c < t;) {
		retval = read(fd, &buf[ptr], 1024 * 1024);
		if (retval == -1 || retval == 0) {
			printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "read error");
			return -1;
		}
		atomic_fetch_add(&prog.current, retval);
		c += retval;
		ptr += retval / sizeof(uint64_t);
		if (ptr == 1024 * 1024 * buf_MB / sizeof(uint64_t) || c == t) { // buffer filled or end of disk
			ptrtmp = ptr;
			if (lseek64(fd, loopstart, SEEK_SET) == -1) {
				printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "lseek back failed");
				return -1;
			}
			for (ptr = 0; ptr < ptrtmp; ptr += retval / sizeof(uint64_t)) {
				retval = write(fd, &buf[ptr], 1024 * 1024);
				if (retval == -1 || retval == 0) {
					printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "write-back error");
					return -1;
				}
			}
			// verify if enabled
			if (params->verify) {
				if (lseek64(fd, loopstart, SEEK_SET) == -1) {
					printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "lseek back failed");
					return -1;
				}
				// read from disk
				for (ptr = 0; ptr < ptrtmp; ptr += retval / sizeof(uint64_t)) {
					retval = read(fd, &vtbuf[ptr], 1024 * 1024);
					if (retval == -1 || retval == 0) {
						printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "re-read error");
						return -1;
					}
				}
				// verify
				if (memcmp(buf, vtbuf, ptrtmp * sizeof(uint64_t)) != 0) {
					printf("\nwrite back maybe failed\n");
					// write back again
				}
			}
			// reset ptr
			ptr = 0;
			loopstart = c;
		}
	}

	// operation finished, stop progression monitoring thread
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

	// finalize
	if (buf != NULL)
		free(buf);
	if (params->verify)
		if (vtbuf != NULL)
			free(vtbuf);
	if (close(fd) == -1) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "close target failed");
		return -1;
	}

	return 0;
}
