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
  if (firefuse_isFile(path, FIREREST_GCODE_FIRE)) {
    res = firefuse_getattr_file(path, stbuf, worker.dce(path).src_gcode_fire.peek().size(), 0666);
  } else {
    res = firerest_getattr_default(path, stbuf);
  }
  if (res == 0) {
    LOGTRACE2("cnc_getattr(%s) stat->st_size:%ldB -> OK", path, (ulong) stbuf->st_size);
  }
  return res;
}

int cnc_open(const char *path, struct fuse_file_info *fi) {
  int result = 0;
    
  if (firefuse_isFile(path, FIREREST_GCODE_FIRE)) {
    if (verifyOpenRW(path, fi, &result)) {
      if ((fi->flags&3) == O_WRONLY && worker.dce(path).snk_gcode_fire.isFresh()) {
	LOGTRACE("snk_gcode_fire.isFresh()");
        result = -EAGAIN;
      } else {
	fi->fh = (uint64_t) (size_t) new SmartPointer<char>(worker.dce(path).src_gcode_fire.get());
      }
    }
  }
  return result;
}

int cnc_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
  int result = 0;
  return result;
}

int cnc_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
  size_t sizeOut = size;
  size_t len;
  (void) fi;

  if (firefuse_isFile(path, FIREREST_GCODE_FIRE) ||
      firefuse_isFile(path, FIREREST_PROPERTIES_JSON) ||
      FALSE) {
    SmartPointer<char> *pJpg = (SmartPointer<char> *) fi->fh;
    sizeOut = firefuse_readBuffer(buf, (char *)pJpg->data(), size, offset, pJpg->size());
  } else {
    LOGERROR2("cnc_read(%s, %ldB) ENOENT", path, size);
    return -ENOENT;
  }

  LOGTRACE3("cnc_read(%s, %ldB) -> %ldB", path, size, sizeOut);
  return sizeOut;
}

int cnc_write(const char *path, const char *buf, size_t bufsize, off_t offset, struct fuse_file_info *fi) {
  assert(offset == 0);
  assert(buf != NULL);
  assert(bufsize >= 0);
  SmartPointer<char> data((char *) buf, bufsize);
  if (firefuse_isFile(path, FIREREST_GCODE_FIRE)) {
    worker.dce(path).snk_gcode_fire.post(data);
    string cmd(buf, bufsize);
    LOGINFO1("DCE::cnc_write() %s", cmd.c_str());
    json_t * response = json_object();
    json_object_set(response, "status", json_string("ACTIVE"));
    json_object_set(response, "gcode", json_string(cmd.c_str()));
    char *responseStr = json_dumps(response, JSON_PRESERVE_ORDER|JSON_COMPACT|JSON_INDENT(0));
    worker.dce(path).src_gcode_fire.post(SmartPointer<char>(responseStr, strlen(responseStr), SmartPointer<char>::MANAGE));
    json_decref(response);
  } else {
    LOGERROR2("cnc_write(%s,%ldB) ENOENT", path, bufsize);
    return -ENOENT;
  }

  return bufsize;
}

int cnc_release(const char *path, struct fuse_file_info *fi) {
  int result = 0;
  LOGTRACE1("cnc_release(%s)", path);
  if (firefuse_isFile(path, FIREREST_GCODE_FIRE)) {
    if (fi->fh) { free( (SmartPointer<char> *) fi->fh); }
  }
  return 0;
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

json_t *json_string(char *value, size_t length) {
  string str(value, length);
  return json_string(str.c_str());
}

void DCE::send(SmartPointer<char> request, json_t*response) {
  string data(request.data(), request.size());

  if (serial_path.empty()) {
    LOGWARN1("DCE::send(%s) serial_path has not been configured", data.c_str());
    json_object_set(response, "status", json_string("WARNING"));
    json_object_set(response, "response", json_string("Serial path not configured"));
  } else if (0==serial_path.compare("mock")) {
    LOGTRACE2("DCE::send(%s) serial_path:%s", data.c_str(), serial_path.c_str());
    json_object_set(response, "status", json_string("DONE"));
    json_object_set(response, "response", json_string("Mock response"));
  } else {
    LOGTRACE2("DCE::send(%s) serial_path:%s", data.c_str(), serial_path.c_str());
  }
}

int DCE::gcode(BackgroundWorker *pWorker) {
  if (snk_gcode_fire.isFresh()) {
    SmartPointer<char> request = snk_gcode_fire.get();
    json_t *response = json_object();
    json_object_set(response, "status", json_string("ACTIVE"));
    json_t *json_cmd = json_string(request.data(), request.size());
    json_object_set(response, "gcode", json_cmd);

    send(request, response);

    char * responseStr = json_dumps(response, JSON_PRESERVE_ORDER|JSON_COMPACT|JSON_INDENT(0));
    LOGDEBUG2("DCE::gcode(%s) -> %s", json_string_value(json_cmd), responseStr);
    src_gcode_fire.post(SmartPointer<char>(responseStr, strlen(responseStr), SmartPointer<char>::MANAGE));
    json_decref(response);
  }

  return 0;
}


