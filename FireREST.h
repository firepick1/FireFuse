#include "FireFUSE.h"

#ifndef FIREREST_CV_H
#define FIREREST_CV_H
#ifdef __cplusplus
extern "C" {
#endif

double cve_seconds();
int cve_save(FuseDataBuffer *pBuffer, const char *path);

#ifdef __cplusplus
}
#endif
#endif
