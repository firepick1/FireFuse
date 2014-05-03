
#include "FireFuse.h"

#ifndef FIREPICK_H
#define FIREPICK_H
#ifdef __cplusplus
extern "C" {
#endif

const char* firepick_status();
const void* firepick_holes(FuseDataBuffer *pJPG);
int firepick_camera_daemon(FuseDataBuffer *pJPG);

#ifdef __cplusplus
}
#endif
#endif
