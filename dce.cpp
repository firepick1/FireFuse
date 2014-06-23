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

DCE::DCE(string name) {
  this->name = name;
  const char *emptyJson = "{}";
  src_gcode_fire.post(SmartPointer<char>((char *)emptyJson, strlen(emptyJson)));
}

DCE::~DCE() {
}

/**
 * Return canonical DCE path. E.g.:
 *   /dev/firefuse/sync/dce/tinyg/gcode.fire => /dce/tinyg/gcode.fire
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
      if (strncmp("/dce", s, 4) == 0) {
        pDce = s;
	s += 4;
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

