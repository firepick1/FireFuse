
#ifndef FIREFUSE_HPP
#define FIREFUSE_HPP
#ifdef __cplusplus
extern "C" {
#endif

#define MAX_GCODE_LEN 255 /* maximum characters in a gcode instruction */

#define FIREREST_1 "/1"
#define FIREREST_CV_1 "/cv/1"
#define FIREREST_CV_1_CVE "/cv/1/cve"
#define FIREREST_CV_1_CVE_OUTPUT_JPG "/cv/1/cve/output.jpg"
#define FIREREST_CV_1_CVE_SUBDIR "/cv/1/cve/"
#define FIREREST_CV_1_IMAGE_JPG "/cv/1/camera.jpg"
#define FIREREST_CV "/cv"
#define FIREREST_CVE "/cve"
#define FIREREST_CAMERA_JPG "/camera.jpg"
#define FIREREST_CAMERA_PNG "/camera.png"
#define FIREREST_MODEL_JSON "/process.json"
#define FIREREST_OUTPUT_JPG "/output.jpg"
#define FIREREST_FIRESIGHT_JSON "/firesight.json"
#define FIREREST_OUTPUT_PNG "/output.png"
#define FIREREST_SAVED_PNG "/saved.png"
#define FIREREST_SAVE_JSON "/save.json"
#define FIREREST_VAR_1_CVE "/var/firefuse/cv/1/cve"
#define FIREREST_VAR "/var/firefuse"

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
} FuseDataBuffer;

#ifdef __cplusplus
}
#endif
#endif
