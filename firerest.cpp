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
#include "firefuse.h"
#include "version.h"

using namespace std;

static string firerest_write_file(const char * path, const char *text) {
  string errMsg;
  FILE * pFile = fopen(path, "w");
  if (pFile == 0) {
    errMsg = "firerest_write_file() could not create: ";
    errMsg += path;
    return errMsg;
  }
  size_t bytesWritten = fwrite( text, 1, strlen(text), pFile);
  fclose(pFile);
  if (bytesWritten != strlen(text)) {
    errMsg = "firerest_config_camera() could not write: ";
    errMsg += path;
    return errMsg;
  }
  return errMsg;
}

static string firerest_config_camera(json_t *pCamera, const char *pCameraName, json_t *pCveMap) {
  string errMsg;
  LOGINFO1("firerest_config_camera() processing camera: %s", pCameraName);
  string cameraPath = "/var/firefuse/cv/";
  cameraPath += pCameraName;
  DIR * cameraDir = opendir(cameraPath.c_str());
  if (cameraDir) {
    LOGINFO1("firererest_config_camera() using existing configuration:", cameraPath.c_str() );
    closedir(cameraDir);
    return errMsg;
  }
  int rc = mkdir(cameraPath.c_str(), 0755);
  if (rc) {
    errMsg = "firerest_config_camera() could not create directory: ";
    errMsg += cameraPath;
    return errMsg;
  }

  string cameraJpgPath(cameraPath);
  cameraJpgPath += "/camera.jpg";
  errMsg = firerest_write_file(cameraJpgPath.c_str(), "");
  if (!errMsg.empty()) { return errMsg; }

  string outputPath(cameraPath);
  outputPath += "/output.jpg";
  errMsg = firerest_write_file(outputPath.c_str(), "");
  if (!errMsg.empty()) { return errMsg; }

  string monitorPath(cameraPath);
  monitorPath += "/monitor.jpg";
  errMsg = firerest_write_file(monitorPath.c_str(), "");
  if (!errMsg.empty()) { return errMsg; }

  json_t *pProfileMap = json_object_get(pCamera, "profile_map");
  if (pProfileMap == 0) {
    errMsg = "firerest_config_camera() missing camera configuration: profile_map";
    return errMsg;
  }
  const char *pProfileName;
  json_t * pProfile;
  json_object_foreach(pProfileMap, pProfileName, pProfile) {
    string profilePath(cameraPath);
    profilePath += "/";
    profilePath += pProfileName;
    rc = mkdir(profilePath.c_str(), 0755);
    if (rc) {
      errMsg = "firerest_config_camera() could not create profile directory: ";
      errMsg += profilePath;
      return errMsg;
    }

    profilePath += "/cve/";
    rc = mkdir(profilePath.c_str(), 0755);
    if (rc) {
      errMsg = "firerest_config_camera() could not create profile cve directory: ";
      errMsg += profilePath;
      return errMsg;
    }
    json_t *pCveNames = json_object_get(pProfile, "cve_names");
    if (pCveNames == 0) {
      errMsg = "firerest_config_camera() missing profile configuration: cve_names";
      return errMsg;
    }
    int index;
    json_t *pCveName;
    json_array_foreach(pCveNames, index, pCveName) {
      const char * pCveNameStr = json_string_value(pCveName);
      json_t *pCve = json_object_get(pCveMap, pCveNameStr);
      if (pCve == 0) {
        errMsg = "firerest_config_camera() missing CVE definition: ";
	errMsg += pCveNameStr;
	return errMsg;
      }
      string cvePath(profilePath);
      cvePath += pCveNameStr;
      cvePath += "/";
      rc = mkdir(cvePath.c_str(), 0755);
      if (rc) {
	errMsg = "firerest_config_camera() could not create cve directory: ";
	errMsg += cvePath;
	return errMsg;
      }
      json_t *pFireSight = json_object_get(pCve, "firesight");
      if (pFireSight == 0) {
        errMsg = "firerest_config_camera() CVE missing definition for: firesight";
	errMsg += pCveNameStr;
	return errMsg;
      }
      char *pFireSightJson = json_dumps(pFireSight, JSON_COMPACT|JSON_PRESERVE_ORDER);
      if (pFireSightJson == 0) {
        errMsg = "firerest_config_camera() could not create firesight json string";
	errMsg += pCveNameStr;
	return errMsg;
      }

      string savedPath(cvePath);
      savedPath += "saved.png";
      errMsg = firerest_write_file(savedPath.c_str(), "");
      if (!errMsg.empty()) { return errMsg; }

      string savePath(cvePath);
      savePath += "save.fire";
      errMsg = firerest_write_file(savePath.c_str(), "");
      if (!errMsg.empty()) { return errMsg; }

      string processPath(cvePath);
      processPath += "process.fire";
      errMsg = firerest_write_file(processPath.c_str(), "");

      string firesightPath(cvePath);
      firesightPath += "firesight.json";
      errMsg = firerest_write_file(firesightPath.c_str(), pFireSightJson);
      free(pFireSightJson);

      json_t *pProperties = json_object_get(pCve, "properties");
      if (pProperties != 0) {
	char *pPropertiesJson = json_dumps(pProperties, JSON_COMPACT|JSON_PRESERVE_ORDER);
	if (pPropertiesJson == 0) {
	  errMsg = "firerest_config_camera() could not create properties json string";
	  errMsg += pCveNameStr;
	  return errMsg;
	}

	string propertiesPath(cvePath);
	propertiesPath += "properties.json";
	errMsg = firerest_write_file(propertiesPath.c_str(), pPropertiesJson);
	free(pPropertiesJson);
      }

      if (!errMsg.empty()) { return errMsg; }
    }
  }

  return errMsg;
}

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
      errMsg = "firerest_config_cv() missing cv configuration: cve_map";
    }
  }
  if (errMsg.empty()) {
    const char *pKey;
    json_t *pCve;
    json_object_foreach(pCveMap, pKey, pCve) {
      LOGINFO1("firerest_config_cv() loaded cve: %s", pKey);
    }
  }

  json_t *pCameraMap = 0;
  if (errMsg.empty()) {
    pCameraMap = json_object_get(pCv, "camera_map");
    if (!pCameraMap) {
      errMsg = "firerest_config_cv() missing cv configuration: camera_map";
    }
  }
  if (errMsg.empty()) {
    const char *pCameraName;
    json_t *pCamera;
    json_object_foreach(pCameraMap, pCameraName, pCamera) {
      errMsg = firerest_config_camera(pCamera, pCameraName, pCveMap);
      if (!errMsg.empty()) {
        break;
      }
    }
  }

  json_decref(pConfig);

  if (!errMsg.empty()) {
    LOGERROR1("%s", errMsg.c_str());
  }
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
