#define _GNU_SOURCE
#include <linux/fs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
//#define __USE_GNU
#include <fcntl.h>

int getDriveTemp(char *drv, int *ret) {
	FILE *fd;
	char cmd[128];
	char buf[128];

	*ret = -99;

	snprintf(cmd, sizeof(cmd), "smartctl -A %s | awk \'$1 == 194 {print $10}\'", drv);

	fd = popen(cmd, "r");
	if (fd == NULL) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __FUNCTION__, "popen failed");
		return -1;
	}

	while (fgets(buf, sizeof(buf), fd) != NULL) {
		*ret = atoi(buf);
	}
	pclose(fd);

	return 0;
}

int getDriveSize(char *drv, uint64_t *ret) {
	int fd;

	fd = open(drv, O_RDONLY | O_DIRECT);
	if (fd == -1) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "target open failed");
		return -1;
	}

	if (ioctl(fd, BLKGETSIZE64, ret) == -1) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "ioctl failed");
		return -1;
	}

	if (close(fd) == -1) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "close failed");
		return -1;
	}

	return 0;
}

int getLogicalSectorSize(char *drv, uint64_t *ret) {
	int fd;

	fd = open(drv, O_RDONLY | O_DIRECT);
	if (fd == -1) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "target open failed");
		return -1;
	}

	if (ioctl(fd, BLKSSZGET, ret) == -1) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "ioctl failed");
		return -1;
	}

	if (close(fd) == -1) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "close failed");
		return -1;
	}

	return 0;
}

int getPhysicalSectorSize(char *drv, uint64_t *ret) {
	int fd;

	fd = open(drv, O_RDONLY | O_DIRECT);
	if (fd == -1) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "target open failed");
		return -1;
	}

	if (ioctl(fd, BLKPBSZGET, ret) == -1) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "ioctl failed");
		return -1;
	}

	if (close(fd) == -1) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "close failed");
		return -1;
	}

	return 0;
}

int CheckIfBlockDevice(char *drv) {
	struct stat sb;
	if (stat(drv, &sb) == -1) {
		printf("%s:%d %s(): %s\n", __FILE__, __LINE__, __func__, "stat failed");
		return -1;
	}
	if (S_ISBLK(sb.st_mode)) {
		return 1;
	} else {
		return 0;
	}
}