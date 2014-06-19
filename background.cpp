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

BackgroundWorker worker;

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

int background_worker() {
  worker.process();
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
  char widthBuf[20];
  char heightBuf[20];
  snprintf(widthBuf, sizeof(widthBuf), "%d", cameraWidth);
  snprintf(heightBuf, sizeof(heightBuf), "%d", cameraHeight);
  const char *argv[] = {
    "CameraNode",
    "-w", 
    widthBuf,
    "-h",
    heightBuf
  };
  int status = firepicam_create(5, argv);
  if (status != 0) {
    LOGERROR1("CameraNode::init() could not initialize camera firepicam_create() -> %d", status);
    throw "Could not initialize camera";
  }
  LOGINFO3("CameraNode::init() %dx%d -> %d", cameraWidth, cameraHeight, status);
}

int CameraNode::async_update_camera_jpg() {
  int processed = 0;
  if (!src_camera_jpg.isFresh() || !src_camera_mat_bgr.isFresh() || !src_camera_mat_gray.isFresh()) {
    processed |= 01;
    LOGTRACE("async_update_camera_jpg() acquiring image");
    src_camera_jpg.get(); // discard current
    
    JPG_Buffer buffer;
    buffer.pData = NULL;
    buffer.length = 0;
    int status = firepicam_acquireImage(&buffer);
    if (status != 0) {
      LOGERROR1("async_update_camera_jpg() firepicam_acquireImage() => %d", status);
      throw "could not acquire image";
    } 
    assert(buffer.pData);
    assert(buffer.length);

    SmartPointer<char> jpg((char *)buffer.pData, buffer.length);
    src_camera_jpg.post(jpg);
    if (src_camera_mat_bgr.isFresh() && src_camera_mat_gray.isFresh()) {
      // proactively update all decoded images to eliminate post-idle refresh lag
      src_camera_mat_bgr.get(); // discard current
      src_camera_mat_gray.get(); // discard current
    } else {
      // To eliminate unnecessary conversion we will only update active Mat
    }
    LOGDEBUG3("async_update_camera_jpg() src_camera_jpg.post(%ldB) %0lx [0]:%0x", (ulong) jpg.size(), (ulong) jpg.data(), (int) *jpg.data());

    std::vector<uchar> vJPG((uchar *)jpg.data(), (uchar *)jpg.data() + jpg.size());
    if (!src_camera_mat_bgr.isFresh()) {
      processed |= 02;
      Mat image = imdecode(vJPG, CV_LOAD_IMAGE_COLOR); 
      src_camera_mat_bgr.post(image);
      LOGTRACE2("async_update_camera_jpg() src_camera_mat_bgr.post(%dx%d)", image.rows, image.cols);
    }
    if (!src_camera_mat_gray.isFresh()) {
      processed |= 04;
      Mat image = imdecode(vJPG, CV_LOAD_IMAGE_GRAYSCALE); 
      src_camera_mat_gray.post(image);
      LOGTRACE2("async_update_camera_jpg() src_camera_mat_gray.post(%dx%d)", image.rows, image.cols);
    }
  }

  return processed;
}

void CameraNode::setOutput(Mat image) {
  if (image.rows==0 || image.cols==0) {
    output_seconds = 0;
    return; // no interest
  }
  LOGTRACE2("CameraNode::setOutput(%dx%d)", image.rows, image.cols);
  output_seconds = cve_seconds();
  vector<uchar> jpgBuf;
  vector<int> param = vector<int>(2);
  param[0] = CV_IMWRITE_PNG_COMPRESSION;
  param[1] = 95; // 0..100; default 95
  imencode(".jpg", image, jpgBuf, param);
  SmartPointer<char> jpg((char *)jpgBuf.data(), jpgBuf.size());
  src_output_jpg.post(jpg);
  LOGTRACE1("CameraNode::setOutput(%ldB)", (ulong)jpg.size());
}

int CameraNode::async_update_monitor_jpg() {
  int processed = 0;
  if (src_monitor_jpg.isFresh()) {
    return processed;
  }
  LOGTRACE("async_update_monitor_jpg()");

  const char *fmt;
  SmartPointer<char> jpg;
  if (cve_seconds() - output_seconds < monitor_duration) {
    jpg = src_output_jpg.get();
    processed |= 0100000;
    fmt = "async_update_monitor_jpg() src_output_jpg.get(%ldB) %0lx [0]:%0lx";
  } else {
    jpg = src_camera_jpg.get();
    processed |= 0200000;
    fmt = "async_update_monitor_jpg() src_camera_jpg.get(%ldB) %0lx [0]:%0lx";
  }
  src_monitor_jpg.post(jpg);

  LOGDEBUG3(fmt, jpg.size(), jpg.data(), (int) *jpg.data());

  return processed;
}

/////////////////////////// BackgroundWorker ///////////////////////////////////

BackgroundWorker::BackgroundWorker() {
  idle_seconds = cve_seconds();
  idle_period = 15;
}

BackgroundWorker::~BackgroundWorker() {
}

void BackgroundWorker::clear() {
  for (std::map<string,CVEPtr>::iterator it=cveMap.begin(); it!=cveMap.end(); ++it){
    delete it->second;
  }
  cveMap.clear();
}

vector<string> BackgroundWorker::getCveNames() {
  vector<string> result;

  for (std::map<string,CVEPtr>::iterator it=cveMap.begin(); it!=cveMap.end(); ++it){
    result.push_back(it->first);
  }

  return result;
}

CVE& BackgroundWorker::cve(string path) {
  string cvePath = cve_path(path.c_str());
  CVEPtr pCve = cveMap[cvePath]; 
  if (!pCve) {
    pCve = new CVE(cvePath);
    cveMap[cvePath] = pCve;
  }
  return *pCve;
}

void BackgroundWorker::idle() {
  LOGTRACE("BackgroundWorker::idle()");
  idle_seconds = cve_seconds();
  SmartPointer<char> discard = cameras[0].src_monitor_jpg.get();
  idle_seconds = cve_seconds();
  LOGINFO2("BackgroundWorker::idle() src_monitor_jpg.get() -> %ldB@%0lx discarded", (ulong) discard.size(), (ulong) discard.data());
}

void BackgroundWorker::processInit() {
  cameras[0].init();
}

int BackgroundWorker::async_process_fire() {
  int processed = 0;
  int mask = 010;
  for (std::map<string,CVEPtr>::iterator it=cveMap.begin(); it!=cveMap.end(); ++it){
    CVEPtr pCve = it->second;
    if (!pCve->src_process_fire.isFresh()) {
      processed |= mask;
      mask <<= 1;
      LOGTRACE1("BackgroundWorker::async_process_fire(%s)", it->first.c_str());
      pCve->process(this);
    }
  }
  return processed;
}

int BackgroundWorker::async_save_fire() {
  int processed = 0;
  int mask = 0100;
  for (std::map<string,CVEPtr>::iterator it=cveMap.begin(); it!=cveMap.end(); ++it){
    CVEPtr pCve = it->second;
    if (!pCve->src_save_fire.isFresh()) {
      processed |= mask;
      mask <<= 1;
      LOGTRACE1("BackgroundWorker::async_save_fire(%s)", it->first.c_str());
      pCve->save(this);
    }
  }
  return processed;
}

int BackgroundWorker::processLoop() {
  int processed = 0;
  processed |= cameras[0].async_update_camera_jpg();
  processed |= cameras[0].async_update_monitor_jpg();
  processed |= async_save_fire();  
  processed |= async_process_fire();  

  if (idle_period && processed == 0 && (cve_seconds() - idle_seconds >= idle_period)) {
    idle();
    processed |= 0400000;
  } 
  if (processed) {
    LOGTRACE1("BackgroundWorkder::processLoop() => %o", processed);
  }
  return processed;
}

void BackgroundWorker::process() {
  try {
    processInit();

    for (;;) {
      processLoop();
      sched_yield();
    }

    LOGINFO("BackgroundWorker::process() exiting");
  } catch (const char * ex) {
    LOGERROR1("BackgroundWorker::process() FATAL EXCEPTION: %s", ex);
  } catch (string ex) {
    LOGERROR1("BackgroundWorker::process() FATAL EXCEPTION: %s", ex.c_str());
  } catch (...) {
    LOGERROR("BackgroundWorker::process() FATAL UNKNOWN EXCEPTION");
  }
}

