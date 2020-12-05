#pragma once

#include <stdint.h>

int getDriveTemp(char *drv, int *ret);
int getDriveSize(char *drv, uint64_t *ret);
int getLogicalSectorSize(char *drv, uint64_t *ret);
int getPhysicalSectorSize(char *drv, uint64_t *ret);
int CheckIfBlockDevice(char *drv);
