#ifndef FIREPICAM_H
#define FIREPICAM_H
#ifdef __cplusplus
extern "C" {
#endif

typedef struct JPG_Buffer {
  char * pData;
  int length;
} JPG_Buffer;

int firepicam_create(int argc, const char **argv);
int firepicam_destroy(int status);
int firepicam_acquireImage(JPG_Buffer *pBuffer);
void firepicam_print_elapsed();

#ifdef __cplusplus
}
#endif
#endif
