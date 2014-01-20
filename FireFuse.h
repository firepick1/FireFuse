
#ifndef FIREFUSE_H
#define FIREFUSE_H
#ifdef __cplusplus
extern "C" {
#endif

#define MAX_GCODE_LEN 255 /* maximum characters in a gcode instruction */

#define STATUS_PATH "/status"
#define HOLES_PATH "/holes"
#define FIRESTEP_PATH "/firestep"
#define FIRELOG_PATH "/firelog"
#define CAM_PATH "/cam.jpg"
#define ECHO_PATH "/echo"

typedef struct {
  char *pData;
  int length;
  long reserved;
} JPG;

#ifdef __cplusplus
}
#endif
#endif
