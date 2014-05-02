
#ifndef FIREFUSE_H
#define FIREFUSE_H
#ifdef __cplusplus
extern "C" {
#endif

#define MAX_GCODE_LEN 255 /* maximum characters in a gcode instruction */

#define FIREREST_VAR_1_CVE "/var/firefuse/cv/1/cve"
#define FIREREST_1 "/1"
#define FIREREST_CV_1 "/cv/1"
#define FIREREST_CV_1_CVE "/cv/1/cve"
#define FIREREST_CV "/cv"
#define FIREREST_CVE "/cve"
#define FIREREST_JPG "/jpg"
#define FIREREST_PNG "/png"
#define FIREREST_SAVE "/save"

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
