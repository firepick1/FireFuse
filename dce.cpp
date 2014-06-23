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

int DCE::gcodePOST(BackgroundWorker *pWorker) {
  return 0;
}

int DCE::gcodeGET(BackgroundWorker *pWorker) {
  return 0;
}

