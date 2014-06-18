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

static string firerest_write_file(const char * path, const char *text) {
  string errMsg;
  FILE * pFile = fopen(path, "w");
  if (pFile == 0) {
    LOGWARN1("firerest_write_file(%s) not created", path);
  } else {
    size_t bytesWritten = fwrite( text, 1, strlen(text), pFile);
    fclose(pFile);
    if (bytesWritten != strlen(text)) {
      LOGWARN1("firerest_write_file(%s) could not write", path);
    }
  }
  return errMsg;
}

static string firerest_config_camera(const char*varPath, json_t *pCamera, const char *pCameraName, json_t *pCveMap, json_t *p_fs_cv) {
  string errMsg;
  LOGINFO1("firerest_config_camera() processing camera: %s", pCameraName);
  string cameraPath(varPath);
  cameraPath += "cv/";
  cameraPath += pCameraName;
  DIR * cameraDir = opendir(cameraPath.c_str());
  if (cameraDir) {
    LOGINFO1("firererest_config_camera() using existing configuration:", cameraPath.c_str() );
    closedir(cameraDir);
    //TODO return errMsg;
  }
  json_t *p_fs_camera = json_object();
  json_object_set(p_fs_cv, pCameraName, p_fs_camera);
  int rc = !cameraDir && mkdir(cameraPath.c_str(), 0755);
  if (rc) {
    LOGWARN1("firerest_config_camera(%s) directory not created", cameraPath.c_str());
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
    errMsg = "firerest_config_camera() missing camera configuration: profile_map";
    return errMsg;
  }
  const char *pProfileName;
  json_t * pProfile;
  json_object_foreach(pProfileMap, pProfileName, pProfile) {
    json_t *p_fs_profile = json_object();
    json_object_set(p_fs_camera, pProfileName, p_fs_profile);
    string profilePath(cameraPath);
    profilePath += "/";
    profilePath += pProfileName;
    rc = mkdir(profilePath.c_str(), 0755);
    if (rc) {
      LOGWARN1("firerest_config_camera(%s) directory not created", profilePath.c_str());
    }

    json_t *p_fs_cve = json_object();
    json_object_set(p_fs_profile, "cve", p_fs_cve);
    profilePath += "/cve/";
    rc = mkdir(profilePath.c_str(), 0755);
    if (rc) {
      LOGWARN1("firerest_config_camera(%s) directory not created", profilePath.c_str());
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
	LOGWARN1("firerest_config_camera(%s) directory not created", cvePath.c_str());
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
	string propertiesPath(cvePath);
	propertiesPath += "properties.json";
	errMsg = firerest_write_file(propertiesPath.c_str(), pPropertiesJson);
	SmartPointer<char> props(pPropertiesJson, strlen(pPropertiesJson));
	factory.cve(cvePath).src_properties_json.post(props);
	free(pPropertiesJson);
      }

      if (!errMsg.empty()) { return errMsg; }

      json_t *p_fs_cvename = json_object();
      json_object_set(p_fs_cve, pCveNameStr, p_fs_cvename);
      json_object_set(p_fs_cvename, "firesight.json", pFireSight);
      json_object_set(p_fs_cvename, "save.fire", json_integer(0444));
      json_object_set(p_fs_cvename, "process.fire", json_integer(0444));
      json_object_set(p_fs_cvename, "saved.png", json_integer(0444));
      json_object_set(p_fs_cvename, "properties.json", pProperties);
    }
  }

  return errMsg;
}

static string firerest_config_cv(const char* varPath, json_t *pConfig, json_t *p_fs_root) {
  string errMsg;

  LOGINFO1("firerest_config_cv(%s) processing configuration: cv", varPath);
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
  json_t *p_fs_cv = NULL;
  if (errMsg.empty()) {
    string cvdir(varPath);
    cvdir += "cv/";
    p_fs_cv = json_object();
    json_object_set(p_fs_root, "cv", p_fs_cv);
    int rc = mkdir(cvdir.c_str(), 0755);
    if (rc) {
      LOGWARN1("firerest_config_cv(%s) directory not created", cvdir.c_str());
    }
  }
  if (errMsg.empty()) {
    const char *pCameraName;
    json_t *pCamera;
    json_object_foreach(pCameraMap, pCameraName, pCamera) {
      errMsg = firerest_config_camera(varPath, pCamera, pCameraName, pCveMap, p_fs_cv);
      if (!errMsg.empty()) {
        break;
      }
    }
  }

  if (!errMsg.empty()) {
    LOGERROR1("%s", errMsg.c_str());
  }
  return errMsg;
}

void firerest_config(const char *pJson) {
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

  char *p_firerest_json = json_dumps(p_firerest, JSON_INDENT(2)|JSON_PRESERVE_ORDER);
  cout << p_firerest_json << endl;
  free (p_firerest_json);
  json_decref(p_firerest);

  json_decref(pConfig);
  if (!errMsg.empty()) {
    throw errMsg;
  }
}
