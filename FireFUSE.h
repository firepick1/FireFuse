#ifndef FIREFUSE_HPP
#define FIREFUSE_HPP
#ifdef __cplusplus
extern "C" {
#endif

#define MAX_GCODE_LEN 255 /* maximum characters in a gcode instruction */


#define STATUS_PATH "/status"
#define HOLES_PATH "/holes"
#define FIRESTEP_PATH "/firestep"
#define FIRELOG_PATH "/firelog"
#define ECHO_PATH "/echo"

typedef struct {
  char *pData;
  int length;
  long reserved;
} FuseDataBuffer;

extern FuseDataBuffer headcam_image;     // perpetually changing image
extern FuseDataBuffer headcam_image_fstat;  // image at time of most recent fstat()

extern FuseDataBuffer* firefuse_allocImage(const char *path, struct fuse_file_info *fi);
extern FuseDataBuffer* firefuse_allocDataBuffer(const char *path, struct fuse_file_info *fi, const char *pData, size_t length);
extern void firefuse_freeDataBuffer(const char *path, struct fuse_file_info *fi);

static inline int firefuse_readBuffer(char *pDst, const char *pSrc, size_t size, off_t offset, size_t len) {
  size_t sizeOut = size;
  if (offset < len) {
    if (offset + sizeOut > len) {
      sizeOut = len - offset;
    }
    memcpy(pDst, pSrc + offset, sizeOut);
  } else {
    sizeOut = 0;
  }

  return sizeOut;
}

#ifdef __cplusplus
}
#endif
#endif
