
#include "FireFUSE.h"

#ifndef FIREPICK_H
#define FIREPICK_H
#ifdef __cplusplus
extern "C" {
#endif

const char* firepick_status();
const void* firepick_holes(FuseDataBuffer *pJPG);
int background_worker(FuseDataBuffer *pJPG);

#ifdef __cplusplus
}
#endif
#endif
