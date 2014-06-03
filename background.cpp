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

FUSE_Cache fusecache;

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

void update_camera_jpg() {
  if (!fusecache.src_camera_jpg.isFresh()) {
    SmartPointer<uchar> jpg((uchar *)buffer.pData, buffer.length);
    LOGTRACE2("update_camera_jpg() src_camera_jpg.post(%ldB) %0lx", jpg.size(), jpg.data());
    fusecache.src_camera_jpg.post(jpg);
  }
}

void update_monitor_jpg() {
  if (!fusecache.src_monitor_jpg.isFresh()) {
    if (cve_seconds() - output_seconds < monitor_seconds) {
      fusecache.src_monitor_jpg.post(fusecache.src_output_jpg.get());
    } else {
      fusecache.src_monitor_jpg.post(fusecache.src_camera_jpg.get());
    }
  }
}

int background_worker(FuseDataBuffer *pJPG) {
  int status = firepicam_create(0, NULL);

  LOGINFO1("background_worker start -> %d", status);
  output_seconds = 0;
  monitor_seconds = 3;

  for (;;) {
    JPG_Buffer buffer;
    buffer.pData = NULL;
    buffer.length = 0;
    
    status = firepicam_acquireImage(&buffer);
    if (status != 0) {
      LOGERROR1("firepicam_acquireImage() => %d", status);
    } else {
      if (fusecache.src_camera_jpg.isFresh()) {
        SmartPointer<uchar> discard = fusecache.src_camera_jpg.get();
	LOGTRACE2("background_worker() src_camera_jpg.get() -> %ldB@%0lx discarded", discard.size(), discard.data());
      }
      update_camera_jpg();
      update_monitor_jpg();
    }
    pJPG->pData = buffer.pData;
    pJPG->length = buffer.length;
  }

  LOGINFO1("background_worker exit -> %d", status);
  firepicam_destroy(status);
}


