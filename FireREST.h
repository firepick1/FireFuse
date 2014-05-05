#include "FireFUSE.h"

#ifndef FIREREST_CV_H
#define FIREREST_CV_H
#ifdef __cplusplus
extern "C" {
#endif

double cve_seconds();

const char* firerest_cv_status();
const void* firerest_cv_holes(FuseDataBuffer *pJPG);
int firerest_cv_camera_daemon(FuseDataBuffer *pJPG);

#ifdef __cplusplus
}
#endif
#endif
