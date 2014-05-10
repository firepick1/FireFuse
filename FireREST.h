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
#define FIREREST_GRAY "/gray"
#define FIREREST_BGR "/bgr"
#define FIREREST_CAMERA_JPG "/camera.jpg"
#define FIREREST_CV "/cv"
#define FIREREST_CVE "/cve"
#define FIREREST_FIRESIGHT_JSON "/firesight.json"
#define FIREREST_MONITOR_JPG "/monitor.jpg"
#define FIREREST_OUTPUT_JPG "/output.jpg"
#define FIREREST_PROCESS_JSON "/process.json"
#define FIREREST_SAVED_PNG "/saved.png"
#define FIREREST_SAVE_JSON "/save.json"

#define FIREREST_VAR "/var/firefuse"

bool cve_isPathPrefix(const char *value, const char * prefix) ;
bool cve_isPathSuffix(const char *path, const char *suffix);
bool cve_isPath(const char *path, int flags);
bool cve_isPathCV(const char *path);
double cve_seconds();
const char * cve_process(FuseDataBuffer *pJPG, const char *path);
int cve_save(FuseDataBuffer *pBuffer, const char *path);
int cve_getattr(const char *path, struct stat *stbuf);
int cve_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi);
int cve_open(const char *path, struct fuse_file_info *fi);
int cve_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int cve_release(const char *path, struct fuse_file_info *fi);
int cve_truncate(const char *path, off_t size);

inline bool verifyOpenR_(const char *path, struct fuse_file_info *fi, int *pResult) {
  if ((fi->flags & 3) != O_RDONLY) {
    LOGERROR1("verifyOpenR_(%s) EACCESS", path);
    (*pResult) = -EACCES;
  }
  return (*pResult) == 0;
}

inline bool verifyOpenRW(const char *path, struct fuse_file_info *fi, int *pResult) {
  if ((fi->flags & O_DIRECTORY)) {
    LOGERROR1("verifyOpenRW(%s) EACCESS", path);
    (*pResult) = -EACCES;
  }
  return (*pResult) == 0;
}

#ifdef __cplusplus
}
#endif
#endif
