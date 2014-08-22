#include <string>
#include <stdio.h>
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
string cameraSourceName = "raspistill";
string cameraSourceConfig = "";

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

void FireREST::create_resource(string path, int perm) {
  LOGINFO2("FireREST::create_resource(%s, %o)", path.c_str(), perm);
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
  LOGINFO3("FireREST::config_camera(%s) %dx%d", cameraPath.c_str(), cameraWidth, cameraHeight);

  json_t *pSource = json_object_get(pCamera, "source");
  if (json_is_object(pSource)) {
    json_t *pSourceName = json_object_get(pSource, "name");
    json_t *pSourceConfig = json_object_get(pSource, "config");
    if (json_is_string(pSourceName)) {
      cameraSourceName = json_string_value(pSourceName);
    }
    assert(strcmp("raspistill", cameraSourceName.c_str()) == 0);
    if (cameraSourceName.compare("raspistill") == 0) {
      if (json_is_string(pSourceConfig)) {
	cameraSourceConfig = json_string_value(pSourceConfig);
	if (cameraSourceConfig.size() == 0) {
	  cameraSourceConfig = "-t 0 -q 65 -bm -s -o /dev/firefuse/cv/1/camera.jpg";
	}
      }
      char buf[256];
      snprintf(buf, sizeof(buf), "%s -w %d -h %d", 
        cameraSourceConfig.c_str(), cameraWidth, cameraHeight);
      cameraSourceConfig = buf;
    }
  }
  LOGINFO3("FireREST::config_camera(%s) source:%s %s", 
    cameraPath.c_str(), cameraSourceName.c_str(), cameraSourceConfig.c_str());

  create_resource(cameraPath + "/camera.jpg", 0666);
  create_resource(cameraPath + "/output.jpg", 0444);
  create_resource(cameraPath + "/monitor.jpg", 0444);

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

      create_resource(cvePath + "firesight.json", 0444);
      create_resource(cvePath + "save.fire", 0444);
      create_resource(cvePath + "process.fire", 0444);
      create_resource(cvePath + "saved.png", 0444);
      create_resource(cvePath + "properties.json", 0666);
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
    LOGINFO1("FireREST::config_cv() loaded cve: %s", pKey);
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

static string config_string(json_t *pJson, const char *propName, const char *defaultValue=NULL){
  string errMsg;
  string result;
  json_t * propValue = json_object_get(pJson, propName);
  if (json_is_string(propValue)) {
    result = json_string_value(propValue);
  } else {
    if (defaultValue) {
      result = defaultValue;
    } else {
      errMsg = "config_string(";
      errMsg += propName;
      errMsg += ") expected string";
      LOGERROR1("%s", errMsg.c_str());
      throw errMsg;
    }
  }
  return result;
}

string FireREST::config_cnc_serial(string dcePath, json_t *pSerial) {
  LOGINFO1("FireREST::config_cnc_serial(%s)", dcePath.c_str());
  string errMsg;
  DCE &dce = worker.dce(dcePath);

  json_t * pPath = json_object_get(pSerial, "path");
  if (!json_is_string(pPath)) {
    errMsg += "FireREST::config_cnc_serial(";
    errMsg += dcePath;
    errMsg += ") missing serial configuration: path";
    LOGERROR1("%s", errMsg.c_str());
    return errMsg;
  }

  string serialPath = config_string(pSerial, "path");
  LOGINFO2("FireREST::config_cnc_serial(%s) path:%s", dcePath.c_str(), serialPath.c_str());

  DCEPtr pDceConflict = worker.getSerialDCE(serialPath);
  if (pDceConflict && pDceConflict != &dce) {
    LOGERROR3("FireREST::config_cnc_serial(%s) serial path conflict with dce(%s): %s", 
      dcePath.c_str(), pDceConflict->getName().c_str(), serialPath.c_str());
    throw "FATAL	: FireREST::config_cnc_serial() configuration conflict";
  } 
  worker.setSerialDCE(serialPath, &dce);
  dce.setSerialPath(serialPath.c_str());

  string stty = config_string(pSerial, "stty", "115200 cs8");
  LOGINFO2("FireREST::config_cnc_serial(%s) stty %s", dcePath.c_str(), stty.c_str());
  dce.setSerialStty(stty.c_str());

  dce.serial_init();

  return errMsg;
}

string FireREST::config_dce(string dcePath, json_t *jdce) {
  string errMsg;
  DCE &dce = worker.dce(dcePath, TRUE);
  json_t *protocol = json_object_get(jdce, "protocol");
  const char *protocolStr = json_is_string(protocol) ? json_string_value(protocol) : "gcode";
  if (0==strcmp("gcode", protocolStr)) {
    create_resource(dcePath + "/gcode.fire", 0666);
  } else if (0==strcmp("tinyg", protocolStr)) {
    create_resource(dcePath + "/gcode.fire", 0666);
  } else {
    errMsg += "FireREST::config_cnc(";
    errMsg += dcePath;
    errMsg += ") unsupported protocol:";
    errMsg += protocolStr;
  }

  json_t * jconfig = json_object_get(jdce, "device-config");
  dce.serial_device_config.clear();
  if (jconfig == NULL) {
    // do not configure device
  } else if (json_is_array(jconfig)) {
    json_t *jvalue;
    size_t index;
    json_array_foreach(jconfig, index, jvalue) {
      if (json_is_string(jvalue)) {
	dce.serial_device_config.push_back(json_string_value(jvalue));
      } else {
	char * value = json_dumps(jvalue, JSON_ENCODE_ANY|JSON_PRESERVE_ORDER);
	dce.serial_device_config.push_back(value);
	free(value);
      }
    }
  } else {
    LOGERROR1("FireREST::config_dce(%s) device-config must be JSON string or array", dcePath.c_str());
  }

  json_t *jserial = json_object_get(jdce, "serial");
  if (jserial) {
    errMsg += config_cnc_serial(dcePath, jserial);
  }

  return errMsg;
}

string FireREST::config_cnc(const char* varPath, json_t *pConfig) {
  string errMsg;

  json_t *jcnc = json_object_get(pConfig, "cnc");
  if (jcnc == 0) {
    return string("FireREST::config_cnc() missing configuration: cnc\n");
  }
  string cncPath(varPath);
  cncPath += "cnc/";
  const char *pKey;
  json_t *jdce;
  json_object_foreach(jcnc, pKey, jdce) {
    string dcePath(cncPath);
    dcePath += pKey;
    LOGINFO1("FireREST::config_cnc() loaded dce: %s", dcePath.c_str());
    errMsg += config_dce(dcePath, jdce);
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

void FireREST::configure_json(const char *pJson) {
  string errMsg;

  json_error_t jerr;
  json_t *pConfig = json_loads(pJson, 0, &jerr);
  if (pConfig == 0) {
    LOGERROR3("FireREST::configure_json() cannot parse json: %s src:%s line:%d", jerr.text, jerr.source, jerr.line);
    throw jerr;
  }

  errMsg += config_cv("/", pConfig);
  errMsg += config_cv("/sync/", pConfig);
  errMsg += config_cnc("/", pConfig);
  errMsg += config_cnc("/sync/", pConfig);

  json_t * bgwkr = json_object_get(pConfig, "background-worker");
  if (json_is_object(bgwkr)) {
    json_t * idle_period = json_object_get(bgwkr, "idle-period");
    if (json_is_number(idle_period)) {
      int period = json_integer_value(idle_period);
      LOGINFO1("FireREST::configure_json() idle_period:%ds", period);
      worker.setIdlePeriod(period);
    }
  }

  char *p_files_json = json_dumps(files.get("/"), JSON_INDENT(2)|JSON_PRESERVE_ORDER);
  cout << p_files_json << endl;
  free (p_files_json);

  if (pConfig) {
    json_decref(pConfig);
  }
  if (!errMsg.empty()) {
    LOGERROR1("FireREST::configure_json() -> %s", errMsg.c_str());
    throw errMsg;
  }
}

char * FireREST::configure_path(const char *path) {
  LOGINFO1("Loading FireREST configuration: %s", path);
  FILE *fConfig = fopen(path, "r");
  if (fConfig == 0) {
    LOGERROR1("FireREST::configure_path(%s) Could not open configuration file", path);
    exit(-ENOENT);
  }
  fseek(fConfig, 0, SEEK_END);
  size_t length = ftell(fConfig);
  fseek(fConfig, 0, SEEK_SET);
  char * pConfigJson = (char *) malloc(length + 1);
  size_t bytesRead = fread(pConfigJson, 1, length, fConfig);
  if (bytesRead != length) {
    LOGERROR3("FireREST::configure_path(%s) configuration file size expected:%ldB actual:%ldB", path, length, bytesRead);
    exit(-EIO);
  }
  pConfigJson[length] = 0;
  fclose(fConfig);

  configure_json(pConfigJson);

  return pConfigJson;
}

///////////////////////////// C methods ////////////////////////////

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

char * firerest_config(const char * path) {
  return firerest.configure_path(path);
}

static const char *RFC4648 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
static const char *HEX = "0123456789abcdef";

string hexFromRFC4648(const char *rfc) {
  assert(strlen(rfc) < 192); char buf[256];
  char *sOut = buf;
  const char *sIn = rfc;
  while (*sIn == ' ') { sIn++; }
  int bits = 0;
  int i = 0;
  for (; sIn[i] && sIn[i] != ' ' && sIn[i] != '='; i++) {
    bits <<= 6;
    switch (sIn[i]) {
      case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G': case 'H': case 'I': case 'J': case 'K': case 'L': case 'M':
      case 'N': case 'O': case 'P': case 'Q': case 'R': case 'S': case 'T': case 'U': case 'V': case 'W': case 'X': case 'Y': case 'Z':
        bits |= sIn[i] - 'A';
	break;
      case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g': case 'h': case 'i': case 'j': case 'k': case 'l': case 'm':
      case 'n': case 'o': case 'p': case 'q': case 'r': case 's': case 't': case 'u': case 'v': case 'w': case 'x': case 'y': case 'z':
        bits |= sIn[i] - 'a' + 26;
	break;
      case '-': bits |= 62; break;
      case '_': bits |= 63; break;
      case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
        bits |= sIn[i] - '0' + 52;
	break;
      default:
	string error(sIn);
	error.insert(i+1, ">?");
	error.insert(i, "<");
	return error;
    }
    if (i % 2 == 1) {
      *sOut++ = HEX[bits >> 8];
      *sOut++ = HEX[(bits >> 4) & 0xf];
      if (sIn[i+1] == '=') { // two pad
        // suppress hex code
      } else {
	*sOut++ = HEX[bits & 0xf]; 
      }
      bits = 0;
    }
  }
  if ((i%2) == 1 && sIn[i] == '=') { // one pad
    *sOut++ = HEX[bits >> 2];
  }
  *sOut++ = 0;

  return string(buf);
}

string hexToRFC4648(const char *hex) {
  assert(strlen(hex) < 256); char buf[256];
  char *sOut = buf;
  const char *sIn = hex;
  while (*sIn == ' ') { sIn++; }
  if (sIn[1] == 'x') { sIn += 2; }
  int bits = 0;
  int i = 0;
  for (; sIn[i] && sIn[i] != ' '; i++) {
    bits <<= 4;
    switch (sIn[i]) {
      case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
        bits |= sIn[i] - '0';
	break;
      case 'a': case 'A': bits |= 0xa; break;
      case 'b': case 'B': bits |= 0xb; break;
      case 'c': case 'C': bits |= 0xc; break;
      case 'd': case 'D': bits |= 0xd; break;
      case 'e': case 'E': bits |= 0xe; break;
      case 'f': case 'F': bits |= 0xf; break;
      default:
	string error(sIn);
	error.insert(i+1, ">?");
	error.insert(i, "<");
	return error;
    }
    if (i % 3 == 2) {
      *sOut++ = RFC4648[bits >> 6];
      *sOut++ = RFC4648[bits & 0x3f];
      bits = 0;
    }
  }
  if (i % 3 == 1) {
    *sOut++ = RFC4648[bits << 2];
    *sOut++ = '=';
  } else if (i % 3 == 2) {
    bits <<= 4;
    *sOut++ = RFC4648[bits >> 6];
    *sOut++ = RFC4648[bits & 0x3f];
    *sOut++ = '=';
    *sOut++ = '=';
  }
  *sOut++ = 0;

  return string(buf);
}

