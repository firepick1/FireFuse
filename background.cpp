#include "FireSight.hpp"
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>
#include <time.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include "firefuse.h"
#include "version.h"
#include "FirePiCam.h"

#include "opencv2/highgui/highgui.hpp"

using namespace cv;
using namespace firesight;

#define STATUS_BUFFER_SIZE 1024

static char status_buffer[STATUS_BUFFER_SIZE];
extern double monitor_seconds;
extern double output_seconds;

DataFactory factory;

const void* firepick_holes(FuseDataBuffer *pJPG) {
  Mat jpg(1, pJPG->length, CV_8UC1, pJPG->pData);
  Mat matRGB = imdecode(jpg, CV_LOAD_IMAGE_COLOR);
  vector<MatchedRegion> matches;

  HoleRecognizer recognizer(26/1.15, 26*1.15);
  recognizer.scan(matRGB, matches);

  imwrite("/home/pi/camcv.bmp", matRGB);

  return pJPG;
}

const char* firepick_status() {
  time_t current_time = time(NULL);
  char timebuf[70];
  strcpy(timebuf, ctime(&current_time));
  timebuf[strlen(timebuf)-1] = 0;

  const char *errorOrWarn = firelog_lastMessage(FIRELOG_WARN);
  if (strlen(errorOrWarn)) {
    return errorOrWarn;
  }

  sprintf(status_buffer, 
    "{\n"
    " 'timestamp':'%s'\n"
    " 'message':'FirePick OK!',\n"
    " 'version':'FireFUSE version %d.%d'\n"
    "}\n",
    timebuf,
    FireFUSE_VERSION_MAJOR, FireFUSE_VERSION_MINOR);
  return status_buffer;
}

int background_worker(FuseDataBuffer *pJPG) {
  return factory.process(pJPG);
}

int DataFactory::update_camera_jpg() {
  if (src_camera_jpg.isFresh()) {
    return 0;
  }
  JPG_Buffer buffer;
  buffer.pData = NULL;
  buffer.length = 0;
  
  status = firepicam_acquireImage(&buffer);
  if (status != 0) {
    LOGERROR1("update_camera_jpg() firepicam_acquireImage() => %d", status);
    throw "could not acquire image";
  } 

  SmartPointer<uchar> jpg((uchar *)buffer.pData, buffer.length);
  LOGTRACE2("update_camera_jpg() src_camera_jpg.post(%ldB) %0lx", jpg.size(), jpg.data());
  src_camera_jpg.post(jpg);

  return 1;
}

int DataFactory::update_monitor_jpg() {
  if (src_monitor_jpg.isFresh()) {
    return 0;
  }
  if (cve_seconds() - output_seconds < monitor_seconds) {
    src_monitor_jpg.post(src_output_jpg.get());
  } else {
    src_monitor_jpg.post(src_camera_jpg.get());
  }
  return 1;
}

int DataFactory::process(FuseDataBuffer *pJPG) {
  int status = firepicam_create(0, NULL);

  LOGINFO1("DataFactory::process() start -> %d", status);
  output_seconds = 0;
  monitor_seconds = 3;

  for (;;) {
    int processed = 0;
    processed += update_camera_jpg();
    processed += update_monitor_jpg();

    if (processed == 0) {
      SmartPointer<uchar> discard = src_camera_jpg.get();
      LOGTRACE2("DataFactory::process() src_camera_jpg.get() -> %ldB@%0lx discarded", discard.size(), discard.data());
    }
  }

  LOGINFO1("DataFactory::process() exit -> %d", status);
  firepicam_destroy(status);
  return 0;
}


