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

FireREST firerest;

/////////////////////////////// JSONFileSystem /////////////////////////////

JSONFileSystem::JSONFileSystem() {
  clear();
}

JSONFileSystem::~JSONFileSystem() { 
  json_t *root = dirMap["/"];
  if (root != NULL) {
    json_decref(root);
  }
}

void JSONFileSystem::clear() {
  dirMap.clear();
  fileMap.clear();
  dirMap["/"] = json_object();
}


json_t * JSONFileSystem::get(const char *path) { 
  json_t * result = dirMap[path];
  if (result == NULL) {
    result = fileMap[path];
  }
  return result;
}

bool JSONFileSystem::isFile(const char *path) { 
  return fileMap[path] ? true : false;
}

bool JSONFileSystem::isDirectory(const char *path) { 
  return dirMap[path] ? true : false;
}

int JSONFileSystem::perms(const char *path) {
  json_t * obj = dirMap[path];
  if (obj != NULL) {
    return 0755; // rwxr_xr_x
  }
  obj = fileMap[path];
  json_t * perms = json_object_get(obj, "perms");
  if (perms == NULL) {
    return 0;
  }

  return json_integer_value(perms);
}

vector<string> JSONFileSystem::fileNames(const char *path) {
  vector<string> result;
  json_t *dir = dirMap[path];
  if (dir) {
    const char *pName;
    json_t * pFile;
    json_object_foreach(dir, pName, pFile) {
      result.push_back(pName);
    }
  }

  return result;
}

vector<string> JSONFileSystem::splitPath(const char *path) {
  assert(path);
  vector<string> result;

  if (*path) {
    assert(*path == '/');
    result.push_back("/");
    const char *pSegmentStart = path+1;

    for (const char *s=pSegmentStart; ;s++) {
      if (*s == '/') {
	if (pSegmentStart < s) {
	  result.push_back(string(pSegmentStart, s));
	  pSegmentStart = s+1;
	} else if (pSegmentStart == s) {
	  pSegmentStart++;
	}
      } else if (*s == 0) {
	if (pSegmentStart < s) {
	  result.push_back(string(pSegmentStart, s));
	}
	break;
      }
    }
  }

  return result;
}

json_t * JSONFileSystem::resolve_file(const char *path) {
  assert(path);
  assert(*path == '/');

  json_t *result = fileMap[path];
  if (result != NULL) {
    return result;
  }
  result = json_object();
  fileMap[path] = result;

  vector<string> segments = splitPath(path);
  string parentPath(segments[0]);
  json_t *parent = dirMap[parentPath];
  int iLast = segments.size()-1;
  for (int i = 1; i < iLast; i++) {
    if (i > 1) {
      parentPath += "/";
    }
    parentPath += segments[i];
    json_t *parent_kid = dirMap[parentPath];
    if (parent_kid == NULL) {
      parent_kid = json_object();
      dirMap[parentPath] = parent_kid;
      dirMap[parentPath+"/"] = parent_kid;
      json_object_set(parent, segments[i].c_str(), parent_kid);
    }
    parent = parent_kid;
  }
  json_object_set(parent, segments[iLast].c_str(), result);

  return result;
}

void JSONFileSystem::create_file(const char *path, int perms) {
  json_t * obj = resolve_file(path);
  json_object_set(obj, "perms", json_integer(perms));
}

/////////////////////////////// FireREST /////////////////////////////

FireREST::FireREST() {
  int rc_mutex = pthread_mutex_init(&processMutex, NULL);
  assert(rc_mutex == 0);
  processCount = 0;
}

FireREST::~FireREST() {
  int rc_mutex = pthread_mutex_destroy(&processMutex);
  assert(rc_mutex == 0);

  json_t *p_files = files.get("/");
  if (p_files) {
    json_decref(p_files);
  }
}

int FireREST::incrementProcessCount() {
  int result;
  ///////////////// CRITICAL SECTION BEGIN ///////////////
  pthread_mutex_lock(&processMutex);			//
  result = ++processCount;
  pthread_mutex_unlock(&processMutex);			//
  ///////////////// CRITICAL SECTION END /////////////////
  return result;
}

int FireREST::decrementProcessCount() {
  int result;
  ///////////////// CRITICAL SECTION BEGIN ///////////////
  pthread_mutex_lock(&processMutex);			//
  result = --processCount;
  pthread_mutex_unlock(&processMutex);			//
  ///////////////// CRITICAL SECTION END /////////////////
  return result;
}

void FireREST::create_file(string path, int perm) {
  LOGINFO2("FIreREST::create_file(%s, %o)", path.c_str(), perm);
  files.create_file(path, perm);
}

string FireREST::config_camera(const char*cv_path, json_t *pCamera, const char *pCameraName, json_t *pCveMap) {
  string errMsg;

  string cameraPath(cv_path);
  cameraPath += "/";
  cameraPath += pCameraName;

  json_t *pWidth = json_object_get(pCamera, "width");
  if (json_is_integer(pWidth)) {
    cameraWidth = json_integer_value(pWidth);
  }

  json_t *pHeight = json_object_get(pCamera, "height");
  if (json_is_integer(pHeight)) {
    cameraHeight = json_integer_value(pHeight);
  }
  LOGINFO1("FireREST::config_camera(%s)", cameraPath.c_str());

  create_file(cameraPath + "/camera.jpg", 0444);
  create_file(cameraPath + "/output.jpg", 0444);
  create_file(cameraPath + "/monitor.jpg", 0444);

  json_t *pProfileMap = json_object_get(pCamera, "profile_map");
  if (pProfileMap == 0) {
    return string("FireREST::config_camera() missing camera configuration: profile_map\n");
  }
  const char *pProfileName;
  json_t * pProfile;
  json_object_foreach(pProfileMap, pProfileName, pProfile) {
    string profilePath(cameraPath);
    profilePath += "/";
    profilePath += pProfileName;
    profilePath += "/cve/";
    json_t *pCveNames = json_object_get(pProfile, "cve_names");
    if (pCveNames == 0) {
      return string("FireREST::config_camera() missing profile configuration: cve_names\n");
    }
    int index;
    json_t *pCveName;
    json_array_foreach(pCveNames, index, pCveName) {
      const char * pCveNameStr = json_string_value(pCveName);
      json_t *pCve = json_object_get(pCveMap, pCveNameStr);
      if (pCve == 0) {
        errMsg = "FireREST::config_camera() missing CVE definition: ";
	errMsg += pCveNameStr;
	errMsg += "\n";
	return errMsg;
      }
      string cvePath(profilePath);
      cvePath += pCveNameStr;
      cvePath += "/";
      json_t *pFireSight = json_object_get(pCve, "firesight");
      if (pFireSight == 0) {
        errMsg = "FireREST::config_camera() CVE missing definition for: firesight";
	errMsg += pCveNameStr;
	errMsg += "\n";
	return errMsg;
      }
      char *pFireSightJson = json_dumps(pFireSight, JSON_COMPACT|JSON_PRESERVE_ORDER);
      if (pFireSightJson == 0) {
        errMsg = "FireREST::config_camera() could not create firesight json string";
	errMsg += pCveNameStr;
	errMsg += "\n";
	return errMsg;
      }

      string firesightPath(cvePath);
      firesightPath += "firesight.json";
      worker.cve(firesightPath, TRUE);
      SmartPointer<char> firesightJson(pFireSightJson, strlen(pFireSightJson), SmartPointer<char>::MANAGE);
      worker.cve(firesightPath).src_firesight_json.post(firesightJson);

      json_t *pProperties = json_object_get(pCve, "properties");
      if (pProperties != 0) {
	char *pPropertiesJson = json_dumps(pProperties, JSON_COMPACT|JSON_PRESERVE_ORDER);
	if (pPropertiesJson == 0) {
	  errMsg = "FireREST::config_camera() could not create properties json string";
	  errMsg += pCveNameStr;
	  return errMsg;
	}
	SmartPointer<char> props(pPropertiesJson, strlen(pPropertiesJson), SmartPointer<char>::MANAGE);
	worker.cve(cvePath).src_properties_json.post(props);
      }

      create_file(cvePath + "firesight.json", 0444);
      create_file(cvePath + "save.fire", 0444);
      create_file(cvePath + "process.fire", 0444);
      create_file(cvePath + "saved.png", 0444);
      create_file(cvePath + "properties.json", 0666);
    }
  }

  return errMsg;
}

string FireREST::config_cv(const char* varPath, json_t *pConfig) {
  string errMsg;

  json_t *pCv = json_object_get(pConfig, "cv");
  if (pCv == 0) {
    return string("FireREST::config_cv() missing configuration: cv\n");
  }
  json_t *pCveMap = 0;
  pCveMap = json_object_get(pCv, "cve_map");
  if (!pCveMap) {
    return string("FireREST::config_cv() missing cv configuration: cve_map\n");
  }
  const char *pKey;
  json_t *pCve;
  json_object_foreach(pCveMap, pKey, pCve) {
    LOGINFO1("firerest_config_cv() loaded cve: %s", pKey);
  }

  json_t *pCameraMap = 0;
  pCameraMap = json_object_get(pCv, "camera_map");
  if (!pCameraMap) {
    return string("FireREST::config_cv() missing cv configuration: camera_map\n");
  }

  string cvPath(varPath);
  cvPath += "cv";
  LOGINFO1("FireREST::config_cv(%s)", cvPath.c_str());

  const char *pCameraName;
  json_t *pCamera;
  json_object_foreach(pCameraMap, pCameraName, pCamera) {
    errMsg += config_camera(cvPath.c_str(), pCamera, pCameraName, pCveMap);
  }

  return errMsg;
}

string FireREST::config_cnc(const char* varPath, json_t *pConfig) {
  string errMsg;

  json_t *pCnc = json_object_get(pConfig, "cnc");
  if (pCnc == 0) {
    return string("FireREST::config_cnc() missing configuration: cv\n");
  }
  string cncPath(varPath);
  cncPath += "cnc/";
  const char *pKey;
  json_t *pDce;
  json_object_foreach(pCnc, pKey, pDce) {
    string dcePath(cncPath);
    dcePath += pKey;
    LOGINFO1("firerest_config_cnc() loaded dce: %s", dcePath.c_str());
    worker.dce(dcePath, TRUE);
  }

  return errMsg;
}

bool FireREST::isSync(const char *pJson) {
  const char *pSlash = pJson;
  while (pSlash && strncmp("/sync/", pSlash, 6)) {
    pSlash = strchr(pSlash+1, '/');
  }
  return pSlash ? true : false;
}

void FireREST::configure(const char *pJson) {
  string errMsg;

  json_error_t jerr;
  json_t *pConfig = json_loads(pJson, 0, &jerr);
  if (pConfig == 0) {
    LOGERROR3("FireREST::configure() cannot parse json: %s src:%s line:%d", jerr.text, jerr.source, jerr.line);
    throw jerr;
  }

  errMsg += config_cv("/", pConfig);
  errMsg += config_cv("/sync/", pConfig);
  errMsg += config_cnc("/", pConfig);
  errMsg += config_cnc("/sync", pConfig);

  char *p_files_json = json_dumps(files.get("/"), JSON_INDENT(2)|JSON_PRESERVE_ORDER);
  cout << p_files_json << endl;
  free (p_files_json);

  if (pConfig) {
    json_decref(pConfig);
  }
  if (!errMsg.empty()) {
    LOGERROR1("%s", errMsg.c_str());
    throw errMsg;
  }
}

void firerest_config(const char *pJson) {
  firerest.configure(pJson);
}

int firerest_getattr_default(const char *path, struct stat *stbuf) {
  int res = -ENOENT;
  if (firerest.isDirectory(path)) {
    memset(stbuf, 0, sizeof(struct stat));
    stbuf->st_uid = getuid();
    stbuf->st_gid = getgid();
    stbuf->st_atime = stbuf->st_mtime = stbuf->st_ctime = time(NULL);
    stbuf->st_nlink = 2;
    stbuf->st_mode = S_IFDIR | firerest.perms(path);
    stbuf->st_size = 4096;
    res = 0;
  } else {
    LOGERROR1("cve_getattr(%s) ENOENT", path);
    res = -ENOENT;
  }
  return res;
}

int firerest_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
  (void) offset;
  (void) fi;

  LOGTRACE1("firerest_readdir(%s)", path);

  filler(buf, ".", NULL, 0);
  filler(buf, "..", NULL, 0);
  if (!firerest.isDirectory(path)) {
    LOGERROR1("firerest_readdir(%s) not a directory", path);
    return -ENOENT;
  }

  vector<string> names = firerest.fileNames(path);
  for (int iFile = 0; iFile < names.size(); iFile++) {
    LOGTRACE2("firerest_readdir(%s) readdir:%s", path, names[iFile].c_str());
    filler(buf, names[iFile].c_str(), NULL, 0);
  }

  return 0;
}

