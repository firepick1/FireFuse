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

bool is_cnc_path(const char *path) {
  for (const char *s = path; s && *s; s++) {
    if (strncmp("/cnc", s, 4) == 0) {
      return TRUE;
    }
  }
  return FALSE;
}

int cnc_getattr(const char *path, struct stat *stbuf) {
  int res = 0;
  if (firefuse_isFile(path, FIREREST_CAMERA_JPG)) {
    res = firefuse_getattr_file(path, stbuf, worker.cameras[0].src_camera_jpg.peek().size(), 0444);
  } else {
    res = firerest_getattr_default(path, stbuf);
  }
  if (res == 0) {
    LOGTRACE2("cnc_getattr(%s) stat->st_size:%ldB -> OK", path, (ulong) stbuf->st_size);
  }
  return res;
}

int cnc_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
  int result = 0;
  return result;
}

int cnc_open(const char *path, struct fuse_file_info *fi) {
  int result = 0;
  return result;
}

int cnc_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
  int result = 0;
  return result;
}

int cnc_write(const char *path, const char *buf, size_t bufsize, off_t offset, struct fuse_file_info *fi) {
  int result = 0;
  return result;
}

int cnc_release(const char *path, struct fuse_file_info *fi) {
  int result = 0;
  return result;
}

int cnc_truncate(const char *path, off_t size) {
  int result = 0;
  return result;
}


DCE::DCE(string name) {
  this->name = name;
  clear();
}

DCE::~DCE() {
}

void DCE::clear() {
  const char *emptyJson = "{}";
  src_gcode_fire.post(SmartPointer<char>((char *)emptyJson, strlen(emptyJson)));
}

/**
 * Return canonical DCE path. E.g.:
 *   /dev/firefuse/sync/cnc/tinyg/gcode.fire => /cnc/tinyg
 *
 * Return empty string if path is not a canonical DCE path
 */
string DCE::dce_path(const char *pPath) {
  if (pPath == NULL) {
    return string();
  }
  const char *pSlash = pPath;
  const char *pDce = NULL;
  for (const char *s=pPath; *s; s++) {
    if (*s == '/') {
      pSlash = s;
      if (strncmp("/cnc/", s, 5) == 0) {
        pDce = s;
	s += 5;
      } else if (pDce) {
        break;
      }
    }
  }
  if (!pDce) {
    return string();
  }
  if (pSlash <= pDce) {
    return string(pDce);
  }
  return string(pDce, pSlash-pDce);
}

int DCE::sendSerial(const char *text) {
  LOGINFO1("sendSerial(%s)", text);
  usleep(1000000); // TBD
  return 0;
}

int DCE::gcode(BackgroundWorker *pWorker) {
  if (!snk_gcode_fire.isFresh()) {
    return 0; // no command
  }
  SmartPointer<char> sp_cmd = snk_gcode_fire.get();
  string cmd(sp_cmd.data(),sp_cmd.size());
  LOGINFO1("DCE::gcode() %s", cmd.c_str());
  json_t * response = json_object();
  json_object_set(response, "status", json_string("ACTIVE"));
  json_object_set(response, "gcode", json_string(cmd.c_str()));
  char *responseStr = json_dumps(response, JSON_PRESERVE_ORDER|JSON_COMPACT|JSON_INDENT(0));
  src_gcode_fire.post(SmartPointer<char>(responseStr, strlen(responseStr), SmartPointer<char>::MANAGE));

  sendSerial(cmd.c_str());
 
  json_object_set(response, "status", json_string("DONE"));
  json_object_set(response, "response", json_string("OK"));
  responseStr = json_dumps(response, JSON_PRESERVE_ORDER|JSON_COMPACT|JSON_INDENT(0));
  src_gcode_fire.post(SmartPointer<char>(responseStr, strlen(responseStr), SmartPointer<char>::MANAGE));
  return 0;
}


