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

const char * fuse_root  = "/dev/firefuse";

int cameraWidth = 800;
int cameraHeight = 200;

static string firerest_config_camera(const char*varPath, json_t *pCamera, const char *pCameraName, json_t *pCveMap, json_t *p_fs_cv) {
  string errMsg;
  LOGINFO1("firerest_config_camera() processing camera: %s", pCameraName);
  string cameraPath(varPath);
  cameraPath += "cv/";
  cameraPath += pCameraName;
  json_t *p_fs_camera = json_object();
  json_object_set(p_fs_cv, pCameraName, p_fs_camera);

  json_t *pWidth = json_object_get(pCamera, "width");
  if (json_is_integer(pWidth)) {
    cameraWidth = json_integer_value(pWidth);
  }

  json_t *pHeight = json_object_get(pCamera, "height");
  if (json_is_integer(pHeight)) {
    cameraHeight = json_integer_value(pHeight);
  }

  json_object_set(p_fs_camera, "camera.jpg", json_integer(0444));
  json_object_set(p_fs_camera, "output.jpg", json_integer(0444));
  json_object_set(p_fs_camera, "monitor.jpg", json_integer(0444));

  json_t *pProfileMap = json_object_get(pCamera, "profile_map");
  if (pProfileMap == 0) {
    return string("firerest_config_camera() missing camera configuration: profile_map");
  }
  const char *pProfileName;
  json_t * pProfile;
  json_object_foreach(pProfileMap, pProfileName, pProfile) {
    json_t *p_fs_profile = json_object();
    json_object_set(p_fs_camera, pProfileName, p_fs_profile);
    string profilePath(cameraPath);
    profilePath += "/";
    profilePath += pProfileName;

    json_t *p_fs_cve = json_object();
    json_object_set(p_fs_profile, "cve", p_fs_cve);
    profilePath += "/cve/";
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

      string firesightPath(cvePath);
      firesightPath += "firesight.json";
      SmartPointer<char> firesightJson(pFireSightJson, strlen(pFireSightJson));
      factory.cve(firesightPath).src_firesight_json.post(firesightJson);
      free(pFireSightJson);

      json_t *pProperties = json_object_get(pCve, "properties");
      if (pProperties != 0) {
	char *pPropertiesJson = json_dumps(pProperties, JSON_COMPACT|JSON_PRESERVE_ORDER);
	if (pPropertiesJson == 0) {
	  errMsg = "firerest_config_camera() could not create properties json string";
	  errMsg += pCveNameStr;
	  return errMsg;
	}
	SmartPointer<char> props(pPropertiesJson, strlen(pPropertiesJson));
	factory.cve(cvePath).src_properties_json.post(props);
	free(pPropertiesJson);
      }

      json_t *p_fs_cvename = json_object();
      json_object_set(p_fs_cve, pCveNameStr, p_fs_cvename);
      json_object_set(p_fs_cvename, "firesight.json", json_integer(0444));
      json_object_set(p_fs_cvename, "save.fire", json_integer(0444));
      json_object_set(p_fs_cvename, "process.fire", json_integer(0444));
      json_object_set(p_fs_cvename, "saved.png", json_integer(0444));
      json_object_set(p_fs_cvename, "properties.json", json_integer(0666));
    }
  }

  return errMsg;
}

static string firerest_config_cv(const char* varPath, json_t *pConfig, json_t *p_fs_root) {
  string errMsg;

  LOGINFO1("firerest_config_cv(%s) processing configuration: cv", varPath);
  json_t *pCv = json_object_get(pConfig, "cv");
  if (pCv == 0) {
    return string("firerest_config_cv() missing configuration: cv");
  }
  json_t *pCveMap = 0;
  pCveMap = json_object_get(pCv, "cve_map");
  if (!pCveMap) {
    return string("firerest_config_cv() missing cv configuration: cve_map");
  }
  const char *pKey;
  json_t *pCve;
  json_object_foreach(pCveMap, pKey, pCve) {
    LOGINFO1("firerest_config_cv() loaded cve: %s", pKey);
  }

  json_t *pCameraMap = 0;
  pCameraMap = json_object_get(pCv, "camera_map");
  if (!pCameraMap) {
    return string("firerest_config_cv() missing cv configuration: camera_map");
  }
  json_t *p_fs_cv = json_object();
  json_object_set(p_fs_root, "cv", p_fs_cv);
  const char *pCameraName;
  json_t *pCamera;
  json_object_foreach(pCameraMap, pCameraName, pCamera) {
    errMsg = firerest_config_camera(varPath, pCamera, pCameraName, pCveMap, p_fs_cv);
    if (!errMsg.empty()) {
      return errMsg;
    }
  }

  return errMsg;
}

json_t * firerest_config(const char *pJson) {
  json_t *p_firerest = json_object();

  json_error_t jerr;
  json_t *pConfig = json_loads(pJson, 0, &jerr);
  string errMsg;
  if (pConfig == 0) {
    LOGERROR3("firerest_config() cannot parse json: %s src:%s line:%d", jerr.text, jerr.source, jerr.line);
    throw jerr;
  }

  errMsg = firerest_config_cv("/var/firefuse/", pConfig, p_firerest);
  if (errMsg.empty()) {
    json_t *p_sync = json_object();
    json_object_set(p_firerest, "sync", p_sync);
    errMsg = firerest_config_cv("/var/firefuse/sync/", pConfig, p_sync);
  }
  if (!errMsg.empty()) {
    LOGERROR1("%s", errMsg.c_str());
  }

  char *p_firerest_json = json_dumps(p_firerest, JSON_INDENT(2)|JSON_PRESERVE_ORDER);
  cout << p_firerest_json << endl;
  free (p_firerest_json);

  json_decref(pConfig);
  if (!errMsg.empty()) {
    json_decref(p_firerest);
    throw errMsg;
  }

  return p_firerest;
}
