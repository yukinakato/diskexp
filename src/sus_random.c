#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE
#include "sus_random.h"
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
	pthread_mutex_t log_mutex;
	pthread_cond_t log_cond;
	char *logfilepath;
	uint64_t elapsed_ns;
	uint64_t numios_r;
	uint64_t numios_w;
} r_stat;

void init_susrandom_params(susrandom_params *p, char *drv, susrandom_rwmode mode, int iosize, int duration, char *logfilepath) {
	p->targetdrv = drv;
	p->rwmode = mode;
	p->iosize = iosize;
	p->durationsec = duration;
	p->logfilepath = logfilepath;
	if (logfilepath != NULL) {
		p->enablelogging = 1;
	} else {
		p->enablelogging = 0;
	}
}

void *printRemainingTime(void *sec) {
	uint64_t count = atomic_load((uint64_t *)sec);
	while (count > 0) {
		printf("\r%02" PRIu64 " h %02" PRIu64 " m %02" PRIu64 " s remaining", count / 3600, count % 3600 / 60, count % 60);
		sleep(1);
		count--;
		atomic_fetch_sub((int *)sec, 1);
	}
	printf("\r%02" PRIu64 " h %02" PRIu64 " m %02" PRIu64 " s remaining", count / 3600, count % 3600 / 60, count % 60);
	printf("\nfinished.\n");
	return NULL;
}

void *CalculateIOPS(void *p) {
	int ret;
	FILE *flog;
	struct timespec t;
	uint64_t temp_r, temp_w, ns;
	uint64_t elapsedsec = 0;
	struct timespec tsa, tsb;
	r_stat *stat = p;

	flog = fopen(stat->logfilepath, "w");
	if (flog == NULL) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "fopen failed");
		return NULL;
	}
	fprintf(flog, "#Time[sec]\tIOs(R)\tIOs(W)\tIOs(R+W)\tIOPS(R)\tIOPS(W)\tIOPS(R+W)\n");

	temp_r = 0;
	temp_w = 0;
	if (pthread_mutex_lock(&stat->log_mutex) != 0) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "mutex lock failed");
		return NULL;
	}
	while (1) {
		clock_gettime(CLOCK_REALTIME, &t);
		t.tv_sec++; // update every seconds
		clock_gettime(CLOCK_MONOTONIC_RAW, &tsa);
		ret = pthread_cond_timedwait(&stat->log_cond, &stat->log_mutex, &t);
		clock_gettime(CLOCK_MONOTONIC_RAW, &tsb);
		ns = getDiffNS(tsa, tsb);
		if (ns > 500 * 1000 * 1000) {
			fprintf(flog, "%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\n", elapsedsec,
					stat->numios_r, stat->numios_w, stat->numios_r + stat->numios_w, (stat->numios_r - temp_r) * 1000 * 1000 * 1000 / ns,
					(stat->numios_w - temp_w) * 1000 * 1000 * 1000 / ns,
					(stat->numios_r + stat->numios_w - temp_r - temp_w) * 1000 * 1000 * 1000 / ns);
			temp_r = stat->numios_r;
			temp_w = stat->numios_w;
			elapsedsec++;
		}
		if (ret != ETIMEDOUT)
			break;
	}
	if (fclose(flog) != 0) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "fclose failed");
	}
	if (pthread_mutex_unlock(&stat->log_mutex) != 0) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "mutex unlock failed");
	}
	return NULL;
}

int SustainedRandomAccess(susrandom_params *params) {
	int fd;
	uint64_t *wbuf, *rbuf;
	pcg32x2_random_t rng;
	uint64_t t, ptr, physicalsectorsize;
	struct timespec tsa, tsb;
	pthread_t pth_remain, pth_log;
	r_stat stat;
	uint64_t remainsec;
	int buf_MB = 256;

	t = 0;
	physicalsectorsize = 0;
	wbuf = NULL;
	rbuf = NULL;

	if (CheckIfBlockDevice(params->targetdrv) != 1) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "target size is not a multiple of physical sector size or is zero");
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
	if ((uint64_t)params->iosize % physicalsectorsize != 0 || (uint64_t)params->iosize < physicalsectorsize) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "iosize is not a multiple of physical sector size");
		return -1;
	}
	if (params->rwmode == susr_rwmode_w || params->rwmode == susr_rwmode_rw) {
		if ((uint64_t)1024 * 1024 * buf_MB % params->iosize != 0) {
			printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "write buffer size is not a multiple of iosize");
			return -1;
		}
	}

	remainsec = (uint64_t)params->durationsec;
	if (remainsec < 1) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "wrong duration");
		return -1;
	}

	// open target
	fd = open(params->targetdrv, O_RDWR | O_DIRECT);
	if (fd == -1) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "open target failed");
		return -1;
	}

	// prepare buffer
	if (params->rwmode == susr_rwmode_w || params->rwmode == susr_rwmode_rw) {
		if (posix_memalign((void **)&wbuf, 1024 * 1024, 1024 * 1024 * buf_MB) != 0) {
			printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "memalign for wbuf failed");
			return -1;
		}
		pcg32x2_srandom_r(&rng, 42u, 42u, 54u, 54u);
		pcg32x2_srandom_r(&rng, time(NULL), time(NULL), (intptr_t)&rng, (intptr_t)&rng);
		for (ptr = 0; ptr < 1024 * 1024 * buf_MB / sizeof(uint64_t); ptr++) {
			wbuf[ptr] = pcg32x2_random_r(&rng);
		}
	}
	if (params->rwmode == susr_rwmode_r || params->rwmode == susr_rwmode_rw) {
		if (posix_memalign((void **)&rbuf, 1024 * 1024, 1024 * 1024 * buf_MB) != 0) {
			printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "memalign for rbuf failed");
			return -1;
		}
		memset(rbuf, '\0', 1024 * 1024 * buf_MB);
	}

	// if logging enabled
	if (params->enablelogging) {
		// create another thread for count down, mutex lock required when accessing stat
		stat.logfilepath = params->logfilepath;
		pthread_mutex_init(&stat.log_mutex, NULL);
		pthread_cond_init(&stat.log_cond, NULL);
		if (pthread_create(&pth_log, NULL, CalculateIOPS, &stat) != 0) {
			printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "create thread failed");
		}
	}

	// fire!
	puts("Starting sustained random access...");
	stat.numios_r = 0;
	stat.numios_w = 0;

	// create another thread for count down, mutex lock required when accessing remainsec
	if (pthread_create(&pth_remain, NULL, printRemainingTime, &remainsec) != 0) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "create thread failed");
	}

	clock_gettime(CLOCK_MONOTONIC_RAW, &tsa);
	ptr = 0;
	if (params->rwmode == susr_rwmode_r) {
		while (1) {
			if (pread(fd, &rbuf[ptr], params->iosize, pcg32x2_boundedrand_r(&rng, t / params->iosize) * params->iosize) == -1) {
				printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "read error");
				return -1;
			}
			ptr += params->iosize / sizeof(uint64_t);
			if (ptr == 1024 * 1024 * buf_MB / sizeof(uint64_t))
				ptr = 0;
			atomic_fetch_add(&stat.numios_r, 1);
			if (atomic_load(&remainsec) == 0)
				break;
		}
	} else if (params->rwmode == susr_rwmode_w) {
		while (1) {
			if (pwrite(fd, &wbuf[ptr], params->iosize, pcg32x2_boundedrand_r(&rng, t / params->iosize) * params->iosize) == -1) {
				printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "write error");
				return -1;
			}
			ptr += params->iosize / sizeof(uint64_t);
			if (ptr == 1024 * 1024 * buf_MB / sizeof(uint64_t))
				ptr = 0;
			atomic_fetch_add(&stat.numios_w, 1);
			if (atomic_load(&remainsec) == 0)
				break;
		}
	} else if (params->rwmode == susr_rwmode_rw) {
		while (1) {
			if (pcg32x2_boundedrand_r(&rng, 2)) { // read
				if (pread(fd, &rbuf[ptr], params->iosize, pcg32x2_boundedrand_r(&rng, t / params->iosize) * params->iosize) == -1) {
					printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "read error");
					return -1;
				}
				ptr += params->iosize / sizeof(uint64_t);
				if (ptr == 1024 * 1024 * buf_MB / sizeof(uint64_t))
					ptr = 0;
				atomic_fetch_add(&stat.numios_r, 1);
			} else { // write
				if (pwrite(fd, &wbuf[ptr], params->iosize, pcg32x2_boundedrand_r(&rng, t / params->iosize) * params->iosize) == -1) {
					printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "write error");
					return -1;
				}
				ptr += params->iosize / sizeof(uint64_t);
				if (ptr == 1024 * 1024 * buf_MB / sizeof(uint64_t))
					ptr = 0;
				atomic_fetch_add(&stat.numios_w, 1);
			}
			if (atomic_load(&remainsec) == 0)
				break;
		}
	} else {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "undefined access mode");
		return -1;
	}
	clock_gettime(CLOCK_MONOTONIC_RAW, &tsb);

	// stop logging thread
	if (params->enablelogging) {
		if (pthread_mutex_lock(&stat.log_mutex) != 0) {
			printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "mutex lock failed");
			return -1;
		}
		if (pthread_cond_signal(&stat.log_cond) != 0) {
			printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "signaling failed");
			return -1;
		}
		if (pthread_mutex_unlock(&stat.log_mutex) != 0) {
			printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "mutex unlock failed");
			return -1;
		}
		if (pthread_join(pth_log, NULL) != 0) {
			printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "pthread join failed");
			return -1;
		}
	}

	// work finished, stop count down thread
	if (pthread_join(pth_remain, NULL) != 0) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "pthread join failed");
		return -1;
	}

	// show statistical result
	printf("Target       : %s\n", params->targetdrv);
	printf("Total IOs(R) : %" PRIu64 "\n", stat.numios_r);
	printf("Total IOs(W) : %" PRIu64 "\n", stat.numios_w);
	printf("IOPS         : %" PRIu64 "\n", (stat.numios_r + stat.numios_w) * 1000 / getDiffMS(tsa, tsb));
	printf("Throughput   : %.2f MB/s\n", (double)(stat.numios_r + stat.numios_w) * params->iosize / getDiffMS(tsa, tsb) / 1000);

	// finalize
	if (params->rwmode == susr_rwmode_w || params->rwmode == susr_rwmode_rw)
		if (wbuf != NULL)
			free(wbuf);
	if (params->rwmode == susr_rwmode_r || params->rwmode == susr_rwmode_rw)
		if (rbuf != NULL)
			free(rbuf);
	if (close(fd) == -1) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "close target failed");
		return -1;
	}
	return 0;
}
