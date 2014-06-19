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

#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"

using namespace cv;
using namespace std;
using namespace firesight;

typedef enum{UI_STILL, UI_VIDEO} UIMode;

/**
 * Return canonical CVE path. E.g.:
 *   /dev/firefuse/sync/cv/1/gray/calc-offset/save.fire => /cv/1/gray/calc-offset
 *
 * Return empty string if path is not a canonical CVE path
 */
string cve_path(const char *pPath) {
  if (pPath == NULL) {
    return string();
  }
  const char *pSlash = pPath;
  const char *pCv = NULL;
  const char *pCve = NULL;
  for (const char *s=pPath; *s; s++) {
    if (*s == '/') {
      pSlash = s;
      if (strncmp("/cv/", s, 4) == 0) {
        pCv = s;
	s += 3;
      } else if (strncmp("/cve/", s, 5) == 0) {
        pCve = s;
	s += 4;
      } else if (pCve) {
        break;
      }
    }
  }
  if (!pCve || !pCv) {
    return string();
  }
  if (pSlash <= pCve) {
    return string(pCv);
  }
  return string(pCv, pSlash-pCv);
}

bool is_cve_path(const char *path) {
  return cve_path(path).empty() ? FALSE : TRUE;
}

static string camera_profile(const char * path) {
  string pathstr(path);
  string result;
  size_t cvePos = pathstr.find("/cve/");

  if (cvePos != string::npos) {
    size_t slashPos = pathstr.rfind("/", cvePos-1);
    if (slashPos != string::npos) {
      result = pathstr.substr(slashPos+1, cvePos-slashPos-1);
    }
  }
  if (result.size() == 0) {
    LOGERROR1("camera_profile(%s) -> UNKNOWN", path);
    return "UNKNOWN";
  }

  LOGTRACE2("camera_profile(%s) -> %s", path, result.c_str());

  return result;
}

int cve_getattr_file(const char *path, struct stat *stbuf, size_t length, int perm=0444) {
  memset(stbuf, 0, sizeof(struct stat));
  stbuf->st_uid = getuid();
  stbuf->st_gid = getgid();
  stbuf->st_atime = stbuf->st_mtime = stbuf->st_ctime = time(NULL);
  stbuf->st_nlink = 1;
  stbuf->st_mode = S_IFREG | perm;
  stbuf->st_size = length;
  return 0;
}

int cve_getattr(const char *path, struct stat *stbuf) {
  int res = 0;

  if (cve_isPathSuffix(path, FIREREST_CAMERA_JPG)) {
    res = cve_getattr_file(path, stbuf, factory.cameras[0].src_camera_jpg.peek().size());
  } else if (cve_isPathSuffix(path, FIREREST_OUTPUT_JPG)) {
    res = cve_getattr_file(path, stbuf, factory.cameras[0].src_output_jpg.peek().size());
  } else if (cve_isPathSuffix(path, FIREREST_MONITOR_JPG)) {
    res = cve_getattr_file(path, stbuf, factory.cameras[0].src_monitor_jpg.peek().size());
  } else if (cve_isPathSuffix(path, FIREREST_SAVED_PNG)) {
    res = cve_getattr_file(path, stbuf, factory.cve(path).src_saved_png.peek().size());
  } else if (cve_isPathSuffix(path, FIREREST_SAVE_FIRE)) {
    res = cve_getattr_file(path, stbuf, factory.cve(path).src_save_fire.peek().size());
  } else if (cve_isPathSuffix(path, FIREREST_PROCESS_FIRE)) {
    res = cve_getattr_file(path, stbuf, factory.cve(path).src_process_fire.peek().size());
  } else if (cve_isPathSuffix(path, FIREREST_PROPERTIES_JSON)) {
    res = cve_getattr_file(path, stbuf, factory.cve(path).src_properties_json.peek().size(), 0666);
  } else if (cve_isPathSuffix(path, FIREREST_FIRESIGHT_JSON)) {
    res = cve_getattr_file(path, stbuf, factory.cve(path).src_firesight_json.peek().size());
  } else if (firerest.isDirectory(path)) {
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

  if (res == 0) {
    LOGTRACE2("cve_getattr(%s) stat->st_size:%ldB -> OK", path, (ulong) stbuf->st_size);
  }
  return res;
}

int cve_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
  (void) offset;
  (void) fi;

  LOGTRACE1("cve_readdir(%s)", path);

  filler(buf, ".", NULL, 0);
  filler(buf, "..", NULL, 0);
  if (!firerest.isDirectory(path)) {
    LOGERROR1("cve_readdir(%s) not a directory", path);
    return -ENOENT;
  }

  vector<string> names = firerest.fileNames(path);
  for (int iFile = 0; iFile < names.size(); iFile++) {
    LOGTRACE2("cve_readdir(%s) readdir:%s", path, names[iFile].c_str());
    filler(buf, names[iFile].c_str(), NULL, 0);
  }

  return 0;
}

static int cve_openVarFile(const char *path, struct fuse_file_info *fi) {
  int result = 0;
  char varPath[255];
  snprintf(varPath, sizeof(varPath), "%s%s", FIREREST_VAR, path);
  FILE *file = fopen(varPath, "rb");
  if (!file) {
    LOGERROR1("cve_openVarFile(%s) fopen failed", varPath);
    return -ENOENT;
  }

  fseek(file, 0, SEEK_END);
  size_t length = ftell(file);
  fseek(file, 0, SEEK_SET);
    
  // TODO fi->fh = (uint64_t) (size_t) firefuse_allocDataBuffer(path, &result, NULL, length);
  if (result == 0) {
    FuseDataBuffer *pBuffer = (FuseDataBuffer *)(size_t) fi->fh;
    size_t bytesRead = fread(pBuffer->pData, 1, length, file);
    if (bytesRead != length) {
      LOGERROR3("cve_openVarFile(%s) read failed %ld != MEMORY-FREE %ld)", path, (ulong) bytesRead, (ulong) length);
      fi->fh = 0;
      free(pBuffer);
    }
  } else {
    result = -ENOMEM;
  }
  if (file) {
    fclose(file);
  }
  return result;
}

static SmartPointer<char> buildErrorMessage(const char* fmt, const char *path, const char * ex) {
  LOGERROR2(fmt, path, ex);
  string errMsg = "{\"error\":\"";
  errMsg.append(ex);
  errMsg.append("\"}");
  return SmartPointer<char>((char *)errMsg.c_str(), errMsg.size()+1);
}

int CVE::process(DataFactory *pFactory) {
  int result = 0;

  double sStart = cve_seconds();
  LOGTRACE1("cve_process(%s) init", name.c_str());
  string pathBuf(name);
  const char *path = pathBuf.c_str(); 
  SmartPointer<char> pipelineJson(src_firesight_json.get());
  char *pModelStr = NULL;
  SmartPointer<char> jsonResult;
  try {
    Pipeline pipeline(pipelineJson.data(), Pipeline::JSON);
    Mat image = _isColor ?
      pFactory->cameras[0].src_camera_mat_bgr.get() :
      pFactory->cameras[0].src_camera_mat_gray.get();
    ArgMap argMap;
    json_t *pProperties = NULL;
    struct stat propertiesStat;   
    string propertiesString;
    SmartPointer<char> properties_json(src_properties_json.get());
    if (properties_json.size()) {
      propertiesString = string(properties_json.data(), properties_json.data()+properties_json.size());
      json_error_t jerr;
      pProperties = json_loads(propertiesString.c_str(), 0, &jerr);
      if (json_is_object(pProperties)) {
	const char * key;
	json_t *pValue;
	json_object_foreach(pProperties, key, pValue) {
	  char *valueStr = json_dumps(pValue, JSON_PRESERVE_ORDER|JSON_COMPACT|JSON_INDENT(0));
	  argMap[key] = valueStr;
	}
      } else {
	LOGERROR2("cve_process(%s) Could not load properties: %s", name.c_str(), propertiesString.c_str());
	throw "could not load properties";
      }
    }
    string savedPath(fuse_root);
    savedPath += name;
    savedPath += FIREREST_SAVED_PNG;
    argMap["saved"] = savedPath.c_str();

    LOGTRACE1("cve_process(%s) process begin", path);
    json_t *pModel = pipeline.process(image, argMap);
    LOGTRACE1("cve_process(%s) process end", path);

    if (json_is_object(pProperties)) {
      const char * key;
      json_t *pValue;
      json_object_foreach(pProperties, key, pValue) {
	free((char *) argMap[key]);
      }
    }
    if (pProperties) {
      json_decref(pProperties);
    }
    int jsonIndent = 0;
    pModelStr = json_dumps(pModel, JSON_PRESERVE_ORDER|JSON_COMPACT|JSON_INDENT(0));
    int modelLen = pModelStr ? strlen(pModelStr) : 0;
    jsonResult = SmartPointer<char>(pModelStr, strlen(pModelStr), SmartPointer<char>::MANAGE);
    json_decref(pModel);
    pFactory->cameras[0].setOutput(image);
    double sElapsed = cve_seconds() - sStart;
    LOGDEBUG3("cve_process(%s) -> JSON %dB %0.3fs", path, modelLen, sElapsed);
  } catch (const char * ex) {
    jsonResult = buildErrorMessage("cve_process(%s) EXCEPTION: %s", path, ex);
  } catch (string ex) {
    jsonResult = buildErrorMessage("cve_process(%s) EXCEPTION: %s", path, ex.c_str());
  } catch (json_error_t ex) {
    string errMsg(ex.text);
    char buf[200];
    snprintf(buf, sizeof(buf), "%s line:%d", ex.text, ex.line);
    errMsg.append(buf);
    jsonResult = buildErrorMessage("cve_process(%s) JSON EXCEPTION: %s", path, errMsg.c_str());
  } catch (...) {
    jsonResult = buildErrorMessage("cve_process(%s) UNKNOWN EXCEPTION: %s", path, "UNKOWN EXCEPTION");
  }
  src_process_fire.post(jsonResult);
  return result;
}

int cve_open(const char *path, struct fuse_file_info *fi) {
  int result = 0;
    
  if (cve_isPathSuffix(path, FIREREST_PROPERTIES_JSON)) {
    if (verifyOpenRW(path, fi, &result)) {
      fi->fh = (uint64_t) (size_t) new SmartPointer<char>(factory.cve(path).src_properties_json.get());
    }
  } else if (verifyOpenR_(path, fi, &result)) {
    if (cve_isPathSuffix(path, FIREREST_PROCESS_FIRE)) {
      fi->fh = (uint64_t) (size_t) new SmartPointer<char>(factory.cve(path).src_process_fire.get());
    } else if (cve_isPathSuffix(path, FIREREST_SAVE_FIRE)) {
      factory.cve(path).save(&factory); // Fast, infrequent operation can be synchronous
      fi->fh = (uint64_t) (size_t) new SmartPointer<char>(factory.cve(path).src_save_fire.peek()); // PEEK for SYNC
    } else if (cve_isPathSuffix(path, FIREREST_CAMERA_JPG)) {
      fi->fh = (uint64_t) (size_t) new SmartPointer<char>(factory.cameras[0].src_camera_jpg.get());
    } else if (cve_isPathSuffix(path, FIREREST_OUTPUT_JPG)) {
      fi->fh = (uint64_t) (size_t) new SmartPointer<char>(factory.cameras[0].src_output_jpg.get());
    } else if (cve_isPathSuffix(path, FIREREST_MONITOR_JPG)) {
      fi->fh = (uint64_t) (size_t) new SmartPointer<char>(factory.cameras[0].src_monitor_jpg.get());
    } else if (cve_isPathSuffix(path, FIREREST_FIRESIGHT_JSON)) {
      fi->fh = (uint64_t) (size_t) new SmartPointer<char>(factory.cve(path).src_firesight_json.get());
    } else if (cve_isPathSuffix(path, FIREREST_SAVED_PNG)) {
      fi->fh = (uint64_t) (size_t) new SmartPointer<char>(factory.cve(path).src_saved_png.get());
    } else {
      result = -ENOENT;
    }
  }

  switch (-result) {
    case ENOENT:
      LOGERROR1("cve_open(%s) error ENOENT", path);
      break;
    case EACCES:
      LOGERROR1("cve_open(%s) error EACCES", path);
      break;
    default:
      if (fi->fh) {
	fi->direct_io = 1;
	LOGTRACE1("cve_open(%s) direct_io:1", path);
      } else {
	LOGTRACE1("cve_open(%s) direct_io:0", path);
      }
      break;
  }

  return result;
}


int cve_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
  size_t sizeOut = size;
  size_t len;
  (void) fi;

  if (cve_isPathSuffix(path, FIREREST_CAMERA_JPG) ||
      cve_isPathSuffix(path, FIREREST_MONITOR_JPG) ||
      cve_isPathSuffix(path, FIREREST_OUTPUT_JPG) ||
      cve_isPathSuffix(path, FIREREST_SAVED_PNG) ||
      cve_isPathSuffix(path, FIREREST_PROCESS_FIRE) ||
      cve_isPathSuffix(path, FIREREST_SAVE_FIRE) ||
      cve_isPathSuffix(path, FIREREST_FIRESIGHT_JSON) ||
      cve_isPathSuffix(path, FIREREST_PROPERTIES_JSON) ||
      FALSE) {
    SmartPointer<char> *pJpg = (SmartPointer<char> *) fi->fh;
    sizeOut = firefuse_readBuffer(buf, (char *)pJpg->data(), size, offset, pJpg->size());
  } else if (fi->fh) { // data file
    FuseDataBuffer *pBuffer = (FuseDataBuffer *) (size_t) fi->fh;
    sizeOut = firefuse_readBuffer(buf, pBuffer->pData, size, offset, pBuffer->length);
  } else {
    LOGERROR2("cve_read(%s, %ldB) ENOENT", path, size);
    return -ENOENT;
  }

  LOGTRACE3("cve_read(%s, %ldB) -> %ldB", path, size, sizeOut);
  return sizeOut;
}

int cve_write(const char *path, const char *buf, size_t bufsize, off_t offset, struct fuse_file_info *fi) {
  assert(offset == 0);
  assert(buf != NULL);
  assert(bufsize >= 0);
  SmartPointer<char> data((char *) buf, bufsize);
  if (cve_isPathSuffix(path, FIREREST_PROPERTIES_JSON)) {
    factory.cve(path).src_properties_json.post(data);
  } else {
    LOGERROR2("cve_write(%s,%ldB) ENOENT", path, bufsize);
    return -ENOENT;
  }

  return bufsize;
}

int cve_release(const char *path, struct fuse_file_info *fi) {
  LOGTRACE1("cve_release(%s)", path);
  if (cve_isPathSuffix(path, FIREREST_PROCESS_FIRE)) {
    if (fi->fh) { free( (SmartPointer<char> *) fi->fh); }
  } else if (cve_isPathSuffix(path, FIREREST_MONITOR_JPG)) {
    if (fi->fh) { free( (SmartPointer<char> *) fi->fh); }
  } else if (cve_isPathSuffix(path, FIREREST_SAVED_PNG)) {
    if (fi->fh) { free( (SmartPointer<char> *) fi->fh); }
  } else if (cve_isPathSuffix(path, FIREREST_OUTPUT_JPG)) {
    if (fi->fh) { free( (SmartPointer<char> *) fi->fh); }
  } else if (cve_isPathSuffix(path, FIREREST_FIRESIGHT_JSON)) {
    if (fi->fh) { free( (SmartPointer<char> *) fi->fh); }
  } else if (cve_isPathSuffix(path, FIREREST_PROPERTIES_JSON)) {
    if (fi->fh) { free( (SmartPointer<char> *) fi->fh); }
  } else if (cve_isPathSuffix(path, FIREREST_CAMERA_JPG)) {
    if (fi->fh) { free( (SmartPointer<char> *) fi->fh); }
  } else if (cve_isPathSuffix(path, FIREREST_SAVE_FIRE)) {
    if (fi->fh) { free( (SmartPointer<char> *) fi->fh); }
  }
  return 0;
}

int cve_truncate(const char *path, off_t size) {
  return 0;
}

double cve_seconds() {
  int64 ticks = getTickCount();
  double ticksPerSecond = getTickFrequency();
  double seconds = ticks/ticksPerSecond;
}

bool cve_isPathSuffix(const char *value, const char * suffix) {
  int suffixLen = strlen(suffix);
  int valueLen = strlen(value);
  if (suffixLen < valueLen) {
    return strcmp(value + valueLen - suffixLen, suffix) == 0;
  }
  return FALSE;
}

CVE::CVE(string name) {
  this->name = name;
  const char *firesight = "[{\"op\":\"putText\", \"text\":\"CVE::CVE()\"}]";
  src_firesight_json.post(SmartPointer<char>((char *)firesight, strlen(firesight)));
  const char *emptyJson = "{}";
  src_save_fire.post(SmartPointer<char>((char *)emptyJson, strlen(emptyJson)));
  src_process_fire.post(SmartPointer<char>((char *)emptyJson, strlen(emptyJson)));
  this->_isColor = strcmp("bgr", camera_profile(name.c_str()).c_str()) == 0;
}

CVE::~CVE() {
}


int CVE::save(DataFactory *pFactory) {
  double sStart = cve_seconds();
  string errMsg;

  Mat image = _isColor ?
    pFactory->cameras[0].src_camera_mat_bgr.get() :
    pFactory->cameras[0].src_camera_mat_gray.get();
  size_t bytes = 0;
  if (image.rows && image.cols) {
    vector<uchar> pngBuf;
    vector<int> param = vector<int>(2);
    param[0] = CV_IMWRITE_PNG_COMPRESSION;
    param[1] = 3;//default(3)  0-9.
    imencode(".png", image, pngBuf, param);
    bytes = pngBuf.size();
    SmartPointer<char> png((char *)pngBuf.data(), bytes);
    src_saved_png.post(png);
    putText(image, "Saved", Point(7, image.rows-6), FONT_HERSHEY_SIMPLEX, 2, Scalar(0,0,0), 3);
    putText(image, "Saved", Point(5, image.rows-8), FONT_HERSHEY_SIMPLEX, 2, Scalar(255,255,255), 3);
    pFactory->cameras[0].setOutput(image);
    LOGTRACE4("CVE::save(%s) %s image saved (%ldB) %0.3fs", name.c_str(), _isColor ? "color" : "gray", bytes, cve_seconds() - sStart);
  } else {
    errMsg = "CVE::save(";
    errMsg.append(name);
    errMsg.append(") => cannot save empty camera image");
  }

  char jsonBuf[255];
  if (errMsg.empty()) {
    snprintf(jsonBuf, sizeof(jsonBuf), "{\"bytes\":%ld}", bytes);
  } else {
    snprintf(jsonBuf, sizeof(jsonBuf), "{\"bytes\":%ld,\"message\":\"%s\"}", 
      bytes, errMsg.c_str());
  }
  SmartPointer<char> json(jsonBuf, strlen(jsonBuf));
  src_save_fire.post(json);
  double sElapsed = cve_seconds() - sStart;
  LOGDEBUG3("CVE::save(%s) -> %ldB %0.3fs", name.c_str(), (ulong) json.size(), sElapsed);

  return errMsg.empty() ? 0 : -ENOENT;
}

