extern "C" {
#define FUSE_USE_VERSION 26
#include <fuse.h>
}

#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>
#include <time.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <math.h>
#include "FireSight.hpp"
#include "FireLog.h"
#include "FireREST.h"
#include "version.h"

using namespace std;

static string firerest_config_cv(json_t *pConfig) {
  string errMsg;

  LOGINFO("firerest_config_cv() processing configuration: cv");
  json_t *pCv = json_object_get(pConfig, "cv");
  if (pCv == 0) {
    errMsg = "firerest_config_cv() missing configuration: cv";
  }
  json_t *pCveMap = 0;
  if (errMsg.empty()) {
    pCveMap = json_object_get(pCv, "cve_map");
    if (!pCveMap) {
      errMsg = "firerest_config_cv() missing configuration: cve_map";
    }
  }
  if (errMsg.empty()) {
    const char *pKey;
    json_t *pCve;
    json_object_foreach(pCveMap, pKey, pCve) {
      LOGINFO1("firerest_config_cv() processing cve: %s", pKey);
    }
  }

  json_t *pCameraMap = 0;
  if (errMsg.empty()) {
    pCameraMap = json_object_get(pCv, "camera_map");
    if (!pCameraMap) {
      errMsg = "firerest_config_cv() missing configuration: camera_map";
    }
  }
  if (errMsg.empty()) {
    const char *pKey;
    json_t *pCamera;
    json_object_foreach(pCameraMap, pKey, pCamera) {
      LOGINFO1("firerest_config_cv() processing camera: %s", pKey);
    }
  }

  json_decref(pConfig);
  return errMsg;
}

void firerest_config(const char *pJson) {
  json_error_t jerr;
  json_t *pConfig = json_loads(pJson, 0, &jerr);
  string errMsg;
  if (pConfig == 0) {
    LOGERROR3("firerest_config() cannot parse json: %s src:%s line:%d", jerr.text, jerr.source, jerr.line);
    throw jerr;
  }

  errMsg = firerest_config_cv(pConfig);

  json_decref(pConfig);
  if (!errMsg.empty()) {
    throw errMsg;
  }
}
