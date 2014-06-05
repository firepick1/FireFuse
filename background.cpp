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
  factory.process(pJPG);
  return 0;
}

/////////////////////////// CameraNode ///////////////////////////////////

CameraNode::CameraNode() {
  output_seconds = 0;
  monitor_duration = 3;
}

CameraNode::~CameraNode() {
  firepicam_destroy(0);
}

void CameraNode::init() {
  int status = firepicam_create(0, NULL);
  if (status != 0) {
    LOGERROR1("DataFactory::process() could not initialize camera -> %d", status);
    throw "Could not initialize camera";
  }
  LOGINFO1("CameraNode::init() -> %d", status);
}

int CameraNode::update_camera_jpg() {
  int actions = 0;
  if (!src_camera_jpg.isFresh() || !src_camera_mat_bgr.isFresh() || !src_camera_mat_gray.isFresh()) {
    actions++;
    LOGTRACE("update_camera_jpg()");
    src_camera_jpg.get(); // make room for post
    
    JPG_Buffer buffer;
    buffer.pData = NULL;
    buffer.length = 0;
    int status = firepicam_acquireImage(&buffer);
    if (status != 0) {
      LOGERROR1("update_camera_jpg() firepicam_acquireImage() => %d", status);
      throw "could not acquire image";
    } 
    assert(buffer.pData);
    assert(buffer.length);

    SmartPointer<uchar> jpg((uchar *)buffer.pData, buffer.length);
    src_camera_jpg.post(jpg);
    if (src_camera_mat_bgr.isFresh() && src_camera_mat_gray.isFresh()) {
      // proactively update all decoded images to eliminate post-idle refresh lag
      src_camera_mat_bgr.get();
      src_camera_mat_gray.get();
    } else {
      // To eliminate unnecessary conversion we will only update active Mat
    }
    LOGDEBUG3("update_camera_jpg() src_camera_jpg.post(%ldB) %0lx [0]:%0x", (ulong) jpg.size(), (ulong) jpg.data(), (int) *jpg.data());

    std::vector<uchar> vJPG(jpg.data(), jpg.data() + jpg.size());
    if (!src_camera_mat_bgr.isFresh()) {
      actions++;
      Mat image = imdecode(vJPG, CV_LOAD_IMAGE_COLOR); 
      src_camera_mat_bgr.post(image);
      LOGTRACE3("update_camera_jpg() src_camera_mat_bgr.post(%ldB) %0lx [0]:%0x", (ulong) jpg.size(), (ulong) jpg.data(), (int) *jpg.data());
    }
    if (!src_camera_mat_gray.isFresh()) {
      actions++;
      Mat image = imdecode(vJPG, CV_LOAD_IMAGE_GRAYSCALE); 
      src_camera_mat_gray.post(image);
      LOGTRACE3("update_camera_jpg() src_camera_mat_gray.post(%ldB) %0lx [0]:%0x", (ulong) jpg.size(), (ulong) jpg.data(), (int) *jpg.data());
    }
  }

  return actions;
}

int CameraNode::update_monitor_jpg() {
  if (src_monitor_jpg.isFresh()) {
    return 0;
  }
  LOGTRACE("update_monitor_jpg()");

  const char *fmt;
  SmartPointer<uchar> jpg;
  if (cve_seconds() - output_seconds < monitor_duration) {
    jpg = src_output_jpg.get();
    fmt = "update_monitor_jpg() src_output_jpg.get(%ldB) %0lx [0]:%0lx";
  } else {
    jpg = src_camera_jpg.get();
    fmt = "update_monitor_jpg() src_camera_jpg.get(%ldB) %0lx [0]:%0lx";
  }
  src_monitor_jpg.post(jpg);

  LOGDEBUG3(fmt, jpg.size(), jpg.data(), (int) *jpg.data());
  return 1;
}

/////////////////////////// DataFactory ///////////////////////////////////

DataFactory::DataFactory() {
  idle_seconds = cve_seconds();
  idle_period = 15;
}

DataFactory::~DataFactory() {
  for (std::map<string,CVEPtr>::iterator it=cveMap.begin(); it!=cveMap.end(); ++it){
    delete it->second;
  }
}

CVE& DataFactory::cve(string path) {
  string cvePath = cve_path(path.c_str());
  CVEPtr pCve = cveMap[cvePath]; 
  if (!pCve) {
    pCve = new CVE();
    cveMap[cvePath] = pCve;
  }
  return *pCve;
}

void DataFactory::idle() {
  LOGTRACE("DataFactory::idle()");
  idle_seconds = cve_seconds();
  SmartPointer<uchar> discard = cameras[0].src_monitor_jpg.get();
  idle_seconds = cve_seconds();
  LOGINFO2("DataFactory::idle() src_monitor_jpg.get() -> %ldB@%0lx discarded", (ulong) discard.size(), (ulong) discard.data());
}

void DataFactory::processInit() {
  cameras[0].init();
}

int DataFactory::processLoop() {
  int processed = 0;
  processed += cameras[0].update_camera_jpg();
  processed += cameras[0].update_monitor_jpg();

  if (processed == 0 && (cve_seconds() - idle_seconds >= idle_period)) {
    idle();
  } 
  return processed;
}

void DataFactory::process(FuseDataBuffer *pJPG) {
  try {
    processInit();

    for (;;) {
      processLoop();
    }

    LOGINFO("DataFactory::process() exiting");
  } catch (const char * ex) {
    LOGERROR1("DataFactory::process() FATAL EXCEPTION: %s", ex);
  } catch (string ex) {
    LOGERROR1("DataFactory::process() FATAL EXCEPTION: %s", ex.c_str());
  } catch (...) {
    LOGERROR("DataFactory::process() FATAL UNKNOWN EXCEPTION");
  }
}

