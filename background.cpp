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

/**
 * Return allocated memory with contents 
 */
SmartPointer<char> loadFile(const char *path, int suffixBytes) {
    LOGINFO1("loadFile(%s)", path);
    FILE *file = fopen(path, "r");
    if (file == 0) {
      LOGERROR1("loadFile() fopen(%s) failed", path);
      ASSERTZERO(-ENOENT);
    }
    fseek(file, 0, SEEK_END);
    size_t length = ftell(file);
    fseek(file, 0, SEEK_SET);
    SmartPointer<char> result(NULL, length+suffixBytes);
    assert(result);
    LOGINFO2("loadFile(%s) fread(%ld)", path, (long)length);
    size_t bytesRead = fread(result.data(), 1, length, file);
    if (bytesRead != length) {
      LOGERROR2("loadFile(), fread failed expected:%ldB actual%ldB", length, bytesRead);
      exit(-EIO);
    }
    LOGINFO2("loadFile(%s) loaded %ldB", path, (long) bytesRead);
    fclose(file);
    return result;
}

/////////////////////////// CameraNode ///////////////////////////////////

CameraNode::CameraNode() {
  output_seconds = 0;
  monitor_duration = 3;
  camera_update_seconds = 1;
  clear();
}

CameraNode::~CameraNode() {
  if (raspistillPID > 0) {
    LOGINFO1("CameraNode::~CameraNode() shutting down raspistill PID:%d", raspistillPID);
    ASSERTZERO(kill(raspistillPID, SIGKILL));
  }
}

void CameraNode::clear() {
  raspistillPID = 0;
}

void CameraNode::init() {
  int status = 0;
  char widthBuf[20];
  char heightBuf[20];
  snprintf(widthBuf, sizeof(widthBuf), "%d", cameraWidth);
  snprintf(heightBuf, sizeof(heightBuf), "%d", cameraHeight);
  const char *raspistill_sh = "/usr/local/bin/raspistill.sh";
  bool isRaspistill  = cameraSourceName.compare("raspistill") == 0;
  if (isRaspistill) {
    struct stat buffer;   
    if (0 == stat(raspistill_sh, &buffer)) {
      SmartPointer<char> jpg = loadFile("/var/firefuse/no-image.jpg");
      update_camera_jpg(jpg);
    } else {
      LOGWARN1("CameraNode::init() raspistill camera source is unavailable:%s", 
        raspistill_sh);
      isRaspistill = FALSE;
      raspistillPID = -ENOENT;
    }
  }
  if (isRaspistill) {
    char cmd[255];
    snprintf(cmd, sizeof(cmd), "%s --launch %s", raspistill_sh, cameraSourceConfig.c_str());
    LOGINFO1("CameraNode::init() %s", cmd);
    ASSERTZERO(BackgroundWorker::callSystem(cmd));
    const char *path_pid = "/var/firefuse/raspistill.PID";

    LOGINFO1("CameraNode::init() fopen(%s)", path_pid);
    FILE *fpid = fopen(path_pid, "r");
    if (fpid == 0) {
      LOGERROR1("CameraNode::init() fopen(%s) failed", path_pid);
      ASSERTZERO(-ENOENT);
    }
    LOGINFO1("CameraNode::init() fseek(%s, SEEK_END)", path_pid);
    fseek(fpid, 0, SEEK_END);
    LOGINFO1("CameraNode::init() ftell(%s)", path_pid);
    size_t length = ftell(fpid);
    LOGINFO1("CameraNode::init() fseek(%s)", path_pid);
    fseek(fpid, 0, SEEK_SET);
    char pidbuf[255];
    assert(length < sizeof(pidbuf)-1);
    LOGINFO2("CameraNode::init() fread(%s, %ld)", path_pid, length);
    size_t bytesRead = fread(pidbuf, 1, length, fpid);
    if (bytesRead != length) {
      LOGERROR2("CameraNode::init(), fread failed expected:%ldB actual%ldB", 
        length, bytesRead);
      exit(-EIO);
    }
    pidbuf[length] = 0;
    LOGINFO1("CameraNode::init() fclose(%s)", path_pid);
    fclose(fpid);
    sscanf(pidbuf,"%d", &raspistillPID);
    LOGINFO1("CameraNode::init() raspistill PID:%d", raspistillPID);
  }
}

//TODOCAM
int CameraNode::async_update_camera_jpg() {
  int processed = 0;
  double now = BackgroundWorker::seconds();
  double elapsed = now - camera_seconds;
  if (elapsed >= camera_update_seconds &&
      ( !src_camera_jpg.isFresh() || 
	!src_camera_mat_bgr.isFresh() || 
	!src_camera_mat_gray.isFresh())) {
    camera_seconds = now;
    processed |= 01;
    LOGTRACE("async_update_camera_jpg() acquiring image");
    
    SmartPointer<char> jpg = src_camera_jpg.get(); // discard current
    if (raspistillPID > 0) {
      LOGDEBUG1("async_update_camera_jpg() SIGUSR1 -> PID%d", raspistillPID);
      int rc = kill(raspistillPID, SIGUSR1); 
      if (rc != 0) {
	const char *details;
	switch (errno) {
	  case EPERM: details = "EPERM"; break;
	  case ESRCH: details = "ESRCH"; break;
	  case EINVAL: details = "EINVAL"; break;
	  default: details = "UNKNOWN ERROR"; break;
	}
	LOGERROR3("CameraNode::async_update_camera_jpg() SIGUSR1->%d: %s %d", 
	  raspistillPID, details, errno);
	exit(-EIO);
      }
    } else {
        // raspistill is configured but unavailable
    }
  }

  return processed;
}

int CameraNode::update_camera_jpg(SmartPointer<char> jpg, int processed) {
  src_camera_jpg.post(jpg);
  if (src_camera_mat_bgr.isFresh() && src_camera_mat_gray.isFresh()) {
    // proactively update all decoded images to eliminate post-idle refresh lag
    src_camera_mat_bgr.get(); // discard current
    src_camera_mat_gray.get(); // discard current
  } else {
    // To eliminate unnecessary conversion we will only update active Mat
  }
  LOGDEBUG3("async_update_camera_jpg() src_camera_jpg.post(%ldB) %0lx [0]:%0x", 
    (ulong) jpg.size(), (ulong) jpg.data(), (int) *jpg.data());

  std::vector<uchar> vJPG((uchar *)jpg.data(), (uchar *)jpg.data() + jpg.size());
  if (!src_camera_mat_bgr.isFresh()) {
    processed |= 02;
    Mat image = imdecode(vJPG, CV_LOAD_IMAGE_COLOR); 
    src_camera_mat_bgr.post(image);
    LOGTRACE2("async_update_camera_jpg() src_camera_mat_bgr.post(%dx%d)", 
      image.rows, image.cols);
  }
  if (!src_camera_mat_gray.isFresh()) {
    processed |= 04;
    Mat image = imdecode(vJPG, CV_LOAD_IMAGE_GRAYSCALE); 
    src_camera_mat_gray.post(image);
    LOGTRACE2("async_update_camera_jpg() src_camera_mat_gray.post(%dx%d)", 
      image.rows, image.cols);
  }

  return processed;
}

void CameraNode::setOutput(Mat image) {
  if (image.rows==0 || image.cols==0) {
    output_seconds = 0;
    return; // no interest
  }
  LOGTRACE2("CameraNode::setOutput(%dx%d)", image.rows, image.cols);
  output_seconds = BackgroundWorker::seconds();
  vector<uchar> jpgBuf;
  vector<int> param = vector<int>(2);
  param[0] = CV_IMWRITE_PNG_COMPRESSION;
  param[1] = 95; // 0..100; default 95
  imencode(".jpg", image, jpgBuf, param);
  SmartPointer<char> jpg((char *)jpgBuf.data(), jpgBuf.size());
  src_output_jpg.post(jpg);
  src_monitor_jpg.get(); // discard stale image
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
  if (BackgroundWorker::seconds() - output_seconds < monitor_duration) {
    jpg = src_output_jpg.get();
    processed |= 01000;
    fmt = "async_update_monitor_jpg() src_output_jpg.get(%ldB) %0lx [0]:%0lx";
  } else {
    jpg = src_camera_jpg.get();
    processed |= 02000;
    fmt = "async_update_monitor_jpg() src_camera_jpg.get(%ldB) %0lx [0]:%0lx";
  }
  src_monitor_jpg.post(jpg);

  LOGDEBUG3(fmt, jpg.size(), jpg.data(), (int) *jpg.data());

  return processed;
}

/////////////////////////// BackgroundWorker ///////////////////////////////////

BackgroundWorker::BackgroundWorker() {
  idle_seconds = BackgroundWorker::seconds();
  idle_period = 15;
}

BackgroundWorker::~BackgroundWorker() {
}

int BackgroundWorker::callSystem(char *cmdbuf) {
  int rc = 0;
  rc = system(cmdbuf);
  if (rc == -1) {
    LOGERROR2("BackgroundWorker::callSystem(%s) -> %d", cmdbuf, rc);
    return rc;
  }
  if (WIFEXITED(rc)) {
    if (WEXITSTATUS(rc)) {
      LOGERROR2("BackgroundWorker::callSystem(%s) -> EXIT %d", cmdbuf, WEXITSTATUS(rc));
      return rc;
    }
  } else if (WIFSTOPPED(rc)) {
      LOGERROR2("BackgroundWorker::callSystem(%s) -> STOPPED %d", cmdbuf, WSTOPSIG(rc));
      return rc;
  } else if (WIFSIGNALED(rc)) {
      LOGERROR2("BackgroundWorker::callSystem(%s) -> SIGNALED %d", cmdbuf, WTERMSIG(rc));
      return rc;
  }
  LOGINFO1("BackgroundWorker::callSystem(%s) OK", cmdbuf);
  return 0;
}

void BackgroundWorker::clear() {
  for (std::map<string,CVEPtr>::iterator it=cveMap.begin(); it!=cveMap.end(); ++it){
    delete it->second;
  }
  cveMap.clear();
  for (std::map<string,DCEPtr>::iterator it=dceMap.begin(); it!=dceMap.end(); ++it){
    delete it->second;
  }
  for (int i=0; i < MAX_CAMERAS; i++) {
    cameras[i].clear();
  }
  dceMap.clear();
  serialMap.clear();
}

vector<string> BackgroundWorker::getCveNames() {
  vector<string> result;

  for (std::map<string,CVEPtr>::iterator it=cveMap.begin(); it!=cveMap.end(); ++it){
    result.push_back(it->first);
  }

  return result;
}

vector<string> BackgroundWorker::getDceNames() {
  vector<string> result;

  for (std::map<string,DCEPtr>::iterator it=dceMap.begin(); it!=dceMap.end(); ++it){
    result.push_back(it->first);
  }

  return result;
}

DCE& BackgroundWorker::dce(string path, bool create) {
  string dcePath = DCE::dce_path(path.c_str());
  if (dcePath.empty()) {
    string err("BackgroundWorkder::dce(");
    err += path;
    err += ") invalid DCE path";
    LOGERROR1("%s", err.c_str());
    throw err;
  }
  DCEPtr pDce = dceMap[dcePath]; 
  if (!pDce) {
    if (!create) {
      string err("BackgroundWorkder::dce(");
      err += path;
      err += ") DCE has not been configured";
      LOGERROR1("%s", err.c_str());
      throw err;
    } 
    pDce = new DCE(dcePath);
    dceMap[dcePath] = pDce;
  }
  return *pDce;
}

CVE& BackgroundWorker::cve(string path, bool create) {
  string cvePath = CVE::cve_path(path.c_str());
  if (cvePath.empty()) {
    string err("BackgroundWorkder::cve(");
    err += path;
    err += ") invalid CVE path";
    LOGERROR1("%s", err.c_str());
    throw err;
  }
  CVEPtr pCve = cveMap[cvePath]; 
  if (!pCve) {
    if (!create) {
      string err("BackgroundWorkder::cve(");
      err += path;
      err += ") CVE has not been configured";
      LOGERROR1("%s", err.c_str());
      throw err;
    } 
    pCve = new CVE(cvePath);
    cveMap[cvePath] = pCve;
  }
  return *pCve;
}

void BackgroundWorker::idle() {
  LOGTRACE("BackgroundWorker::idle()");
  idle_seconds = BackgroundWorker::seconds();
  SmartPointer<char> discard = cameras[0].src_monitor_jpg.get();
  idle_seconds = BackgroundWorker::seconds();
  LOGINFO2("BackgroundWorker::idle() src_monitor_jpg.get() -> %ldB@%0lx discarded", (ulong) discard.size(), (ulong) discard.data());
}

void BackgroundWorker::processInit() {
  cameras[0].init();
}

int BackgroundWorker::async_gcode_fire() {
  int processed = 0;
  int mask = 0100;
  for (std::map<string,DCEPtr>::iterator it=dceMap.begin(); it!=dceMap.end(); ++it){
    DCEPtr pDce = it->second;
    if (pDce->snk_gcode_fire.isFresh()) {
      processed |= mask;
      LOGTRACE1("BackgroundWorker::async_gcode_fire(%s)", it->first.c_str());
      pDce->gcode(this);
    }
  }
  return processed;
}

int BackgroundWorker::async_process_fire() {
  int processed = 0;
  int mask = 010;
  for (std::map<string,CVEPtr>::iterator it=cveMap.begin(); it!=cveMap.end(); ++it){
    CVEPtr pCve = it->second;
    if (!pCve->src_process_fire.isFresh()) {
      processed |= mask;
      LOGTRACE1("BackgroundWorker::async_process_fire(%s)", it->first.c_str());
      pCve->process(this);
    }
  }
  return processed;
}

int BackgroundWorker::async_save_fire() {
  int processed = 0;
  int mask = 020;
  for (std::map<string,CVEPtr>::iterator it=cveMap.begin(); it!=cveMap.end(); ++it){
    CVEPtr pCve = it->second;
    if (!pCve->src_save_fire.isFresh()) {
      processed |= mask;
      LOGTRACE1("BackgroundWorker::async_save_fire(%s)", it->first.c_str());
      pCve->save(this);
    }
  }
  return processed;
}

double BackgroundWorker::seconds() {
  int64 ticks = getTickCount();
  double ticksPerSecond = getTickFrequency();
  double seconds = ticks/ticksPerSecond;
}

int BackgroundWorker::processLoop() {
  int processed = 0;
  processed |= async_gcode_fire();
  processed |= cameras[0].async_update_camera_jpg();
  processed |= async_save_fire();  
  processed |= async_process_fire();  
  processed |= cameras[0].async_update_monitor_jpg();

  if (idle_period && processed == 0 && (BackgroundWorker::seconds() - idle_seconds >= idle_period)) {
    idle();
    processed |= 04000;
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

