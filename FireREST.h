#include "FireFUSE.h"

#ifndef FIREREST_CV_H
#define FIREREST_CV_H
#ifdef __cplusplus
extern "C" {
#endif

#ifndef bool
#define bool int
#define true 1
#define false 0
#endif

enum CVE_Path {
    CVEPATH_CAMERA_SAVE=1,
    CVEPATH_CAMERA_LOAD=2,
    CVEPATH_PROCESS_JSON=4
};

#define FIREREST_1 "/1"
#define FIREREST_CAMERA_JPG "/camera.jpg"
#define FIREREST_CV_1 "/cv/1"
#define FIREREST_CV_1_CVE "/cv/1/cve"
#define FIREREST_CV_1_CVE_OUTPUT_JPG "/cv/1/cve/output.jpg"
#define FIREREST_CV_1_CVE_SUBDIR "/cv/1/cve/"
#define FIREREST_CV_1_IMAGE_JPG "/cv/1/camera.jpg"
#define FIREREST_CV_1_MONITOR_JPG "/cv/1/monitor.jpg"
#define FIREREST_CV "/cv"
#define FIREREST_CVE "/cve"
#define FIREREST_FIRESIGHT_JSON "/firesight.json"
#define FIREREST_MONITOR_JPG "/monitor.jpg"
#define FIREREST_OUTPUT_JPG "/output.jpg"
#define FIREREST_PROCESS_JSON "/process.json"
#define FIREREST_SAVED_PNG "/saved.png"
#define FIREREST_SAVE_JSON "/save.json"
#define FIREREST_VAR_1_CVE "/var/firefuse/cv/1/cve"
#define FIREREST_VAR_1_MONITOR_JPG "/var/firefuse/cv/1/monitor.jpg"
#define FIREREST_VAR "/var/firefuse"

bool cve_isPath(const char *path, int flags);
double cve_seconds();
const char * cve_process(FuseDataBuffer *pJPG, const char *path);
int cve_save(FuseDataBuffer *pBuffer, const char *path);

#ifdef __cplusplus
}
#endif
#endif
