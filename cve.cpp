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

int max_json_len = 1024;

typedef enum{UI_STILL, UI_VIDEO} UIMode;

typedef class CachedJPG {
  private:
    FuseDataBuffer *pCachedJPG;
    void freeBuffer(const char *path) {
      if (pCachedJPG) {
	LOGTRACE2("CachedJPG::freeBuffer(%s) MEMORY-FREE %ldB", path, (ulong) pCachedJPG->length);
        free(pCachedJPG);
	pCachedJPG = NULL;
      }
    }

  public:
    CachedJPG() {
      pCachedJPG = NULL;
    }
    ~CachedJPG() {
      freeBuffer("CachedJPG::destructor");
    }

    int push(const char* path, FuseDataBuffer *pValue) {
      freeBuffer(path);
      pCachedJPG = pValue;
      return pCachedJPG ? pCachedJPG->length: 0;
    }
    FuseDataBuffer *peek(const char *path) {
      return pCachedJPG;
    }
    FuseDataBuffer *pop(const char *path) {
      FuseDataBuffer *pBuffer = pCachedJPG;
      pCachedJPG = NULL;
      return pBuffer;
    }
} CachedJPGType;

class CveCam {
  private:
    CachedJPGType cameraJPG;
    CachedJPGType outputJPG;
    CachedJPGType monitorJPG;
    Mat output_image;

    FuseDataBuffer *createOutputJPG(const char *path, int *pResult) {
      FuseDataBuffer *pJPG = NULL;
      if (output_image.rows && output_image.cols) {
	vector<uchar> vJPG;
	imencode(".jpg", output_image, vJPG);
	SmartPointer<char> jpg((char *)vJPG.data(), vJPG.size());
	factory.cameras[0].src_output_jpg.post(jpg);
	pJPG = firefuse_allocDataBuffer(path, pResult, (const char*) vJPG.data(), vJPG.size());
	LOGTRACE2("CveCam::createOutputJPG(%s) %ldB", path, (ulong) pJPG->length);
      } else {
        factory.cameras[0].src_output_jpg.post(factory.cameras[0].src_camera_jpg.get());
	pJPG = produceCameraJPG(path, pResult);
	LOGTRACE2("CveCam::createOutputJPG(%s) unavailable (using camera image) %ldB", path, (ulong) pJPG->length);
      }
      return pJPG;
    }

  public:
    CveCam() { }

    void setOutput(Mat value) {
       output_image = value;
       factory.cameras[0].temp_set_output_seconds();
    }

    FuseDataBuffer *produceCameraJPG(const char *path, int *pResult) {
      FuseDataBuffer *pJPG = cameraJPG.pop(path);
      if (!pJPG) {
	pJPG = firefuse_allocDataBuffer(path, pResult, headcam_image.pData, headcam_image.length);
      }
      LOGTRACE2("CveCam::produceCameraJPG(%s) %ldB", path, (ulong) pJPG->length);
      return pJPG;
    }

    FuseDataBuffer *produceOutputJPG(const char *path, int *pResult) {
      FuseDataBuffer *pJPG = outputJPG.pop(path);
      if (!pJPG) {
	pJPG = createOutputJPG(path, pResult);
      }
      LOGTRACE2("CveCam::produceOutputJPG(%s) %ldB", path, (ulong) pJPG->length);
      return pJPG;
    }

} cveCam[1];

string cve_path(const char *pPath) {
  assert(pPath);
  const char *pSlash = pPath;
  for (const char *s=pPath; *s; s++) {
    if (*s == '/') {
      pSlash = s;
    }
  }
  return string(pPath, pSlash-pPath);
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

static string buildVarPath(const char * path, const char *fName, int parent=1) {
  char buf[255];
  int n = snprintf(buf, sizeof(buf), "%s%s", FIREREST_VAR, path);
  if (n <= 0 || n == sizeof(buf) || n + strlen(fName) >= sizeof(buf)) {
    LOGERROR1("buildVarPath(%s) buffer overflow", path);
    return "buildVarPathERROR";
  }
  char *s = buf+n;
  while (s-- > buf) {
    if (*s == '/' && parent-- == 1) {
      sprintf(s, "%s", fName);
    }
  }

  return string(buf);
}

int cve_getattr_file(const char *path, struct stat *stbuf, size_t length) {
  memset(stbuf, 0, sizeof(struct stat));
  stbuf->st_uid = getuid();
  stbuf->st_gid = getgid();
  stbuf->st_atime = stbuf->st_mtime = stbuf->st_ctime = time(NULL);
  stbuf->st_nlink = 1;
  stbuf->st_mode = S_IFREG | 0444;
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
  } else {
    string sVarPath = buildVarPath(path, "", 0);
    const char* pVarPath = sVarPath.c_str();
    struct stat filestatus;
    res = stat( pVarPath, &filestatus );
    if (res) {
      LOGERROR3("cve_getattr(%s) stat(%s) -> %d", path, pVarPath, res);
    }
    if (res == 0) {
      (*stbuf) = filestatus;
      if (stbuf->st_mode & S_IFDIR) {
	stbuf->st_mode = S_IFDIR | 0755;
      } else {
	stbuf->st_mode = S_IFREG | 0444;
      }
    }
    if (cve_isPathSuffix(path, FIREREST_PROCESS_JSON)) {
      //TODO cveCam[0].sizeCameraJPG(path, &res); // get current picture but ignore size
      //TODO stbuf->st_size = max_json_len;
    } else if (cve_isPathSuffix(path, FIREREST_SAVE_JSON)) {
      //TODO cveCam[0].sizeCameraJPG(path, &res); // get current picture but ignore size
      //TODO stbuf->st_size = max_json_len;
    }
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

  string sVarPath = buildVarPath(path, "", 0);
  const char* pVarPath = sVarPath.c_str();

  filler(buf, ".", NULL, 0);
  filler(buf, "..", NULL, 0);
  DIR *dirp = opendir(pVarPath);
  if (!dirp) {
    LOGERROR2("cve_readdir(%s) opendir(%s) failed", path, pVarPath);
    return -ENOENT;
  }

  struct dirent * dp;
  while ((dp = readdir(dirp)) != NULL) {
    LOGTRACE2("cve_readdir(%s) readdir:%s", path, dp->d_name);
    filler(buf, dp->d_name, NULL, 0);
  }
  (void)closedir(dirp);

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

int cve_save(const char *path) {
  double sStart = cve_seconds();
  string errMsg;

  string savedPath = buildVarPath(path, FIREREST_SAVED_PNG);
  LOGTRACE2("cve_save(%s) saving to %s", path, savedPath.c_str());
  bool isColor = strcmp("bgr", camera_profile(path).c_str()) == 0;
  SmartPointer<char> camera_jpg = factory.cameras[0].src_camera_jpg.get();
  Mat image;
  if (isColor) {
    image = factory.cameras[0].src_camera_mat_bgr.get();
  } else {
    image = factory.cameras[0].src_camera_mat_gray.get();
  }
  CVE& cve = factory.cve(path);
  if (image.rows && image.cols) {
    vector<uchar> pngBuf;
    vector<int> param = vector<int>(2);
    param[0] = CV_IMWRITE_PNG_COMPRESSION;
    param[1] = 3;//default(3)  0-9.
    imencode(".png", image, pngBuf, param);
    SmartPointer<char> png((char *)pngBuf.data(), pngBuf.size());
    cve.src_saved_png.post(png);
    LOGTRACE4("cve_save(%s) %s image saved (%ldB) %0.3fs", path, isColor ? "color" : "gray", pngBuf.size(), cve_seconds() - sStart);
  } else {
    errMsg = "cve_save(";
    errMsg.append(path);
    errMsg.append(") => cannot save empty camera image");
  }

  char jsonBuf[255];
  if (errMsg.empty()) {
    snprintf(jsonBuf, sizeof(jsonBuf), "{\"status\":{\"time\":\"%.1f\",\"result\":\"OK\"}}\n", cve_seconds());
  } else {
    snprintf(jsonBuf, sizeof(jsonBuf), "{\"status\":{\"time\":\"%.1f\",\"result\":\"ENOENT\",\"message\":\"%s\"}}\n", 
      cve_seconds(), errMsg.c_str());
  }
  SmartPointer<char> json(jsonBuf, strlen(jsonBuf)+1);
  cve.src_save_fire.post(json);
  double sElapsed = cve_seconds() - sStart;
  LOGDEBUG3("cve_save(%s) -> %ldB %0.3fs", path, (ulong) json.size(), sElapsed);

  return errMsg.empty() ? 0 : -ENOENT;
}

static SmartPointer<char> buildErrorMessage(const char* fmt, const char *path, const char * ex) {
  LOGERROR2(fmt, path, ex);
  string errMsg = "{\"error\":\"";
  errMsg.append(ex);
  errMsg.append("\"}");
  return SmartPointer<char>((char *)errMsg.c_str(), errMsg.size()+1);
}

void cve_process(const char *path, int *pResult) {
  assert(path);
  assert(pResult);
  double sStart = cve_seconds();
  string firesightPath = buildVarPath(path, FIREREST_FIRESIGHT_JSON);
  LOGTRACE2("cve_process(%s) loading JSON: %s", path, firesightPath.c_str());
  string propertiesPath = buildVarPath(path, FIREREST_PROPERTIES_JSON);
  LOGTRACE2("cve_process(%s) loading JSON: %s", path, propertiesPath.c_str());
  char *pModelStr = NULL;
  FuseDataBuffer *pJSON = NULL;
  *pResult = 0;
  SmartPointer<char> jsonResult;
  try {
    Pipeline pipeline(firesightPath.c_str(), Pipeline::PATH);
    SmartPointer<char> jpg(factory.cameras[0].src_camera_jpg.get());
    const uchar * pJPGBytes = (uchar*)jpg.data();
    std::vector<uchar> vJPG (pJPGBytes, pJPGBytes + jpg.size() / sizeof(uchar) );
    LOGTRACE1("cve_process(%s) decode image", path);
    bool isColor = strcmp("bgr", camera_profile(path).c_str()) == 0;
    Mat image = imdecode(vJPG, isColor ? CV_LOAD_IMAGE_COLOR : CV_LOAD_IMAGE_GRAYSCALE); 
    string savedPath = buildVarPath(path, FIREREST_SAVED_PNG);
    ArgMap argMap;
    json_t *pProperties = NULL;
    struct stat propertiesStat;   
    if (stat (propertiesPath.c_str(), &propertiesStat) == 0) {
      string propertiesString;
      ifstream ifs(propertiesPath.c_str());
      stringstream propertiesStream;
      propertiesStream << ifs.rdbuf();
      propertiesString = propertiesStream.str();
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
        LOGERROR2("cve_process(%s) Could not load properties: %s", path, propertiesString.c_str());
      }
    }
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
    jsonResult = SmartPointer<char>(pModelStr, 0);
    json_decref(pModel);
    cveCam[0].setOutput(image);
    double sElapsed = cve_seconds() - sStart;
    LOGDEBUG3("cve_process(%s) -> JSON %dB %0.3fs", path, pJSON->length, sElapsed);
  } catch (char * ex) {
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
  factory.cve(path).src_process_fire.post(jsonResult);
}

int cve_open(const char *path, struct fuse_file_info *fi) {
  int result = 0;
    
  if (verifyOpenR_(path, fi, &result)) {
    if (cve_isPathSuffix(path, FIREREST_PROCESS_JSON)) {
      fi->fh = (uint64_t) (size_t) new SmartPointer<char>(factory.cve(path).src_process_fire.get());
    } else if (cve_isPathSuffix(path, FIREREST_SAVE_JSON)) {
      fi->fh = (uint64_t) (size_t) new SmartPointer<char>(factory.cve(path).src_save_fire.get());
    } else if (cve_isPathSuffix(path, FIREREST_CAMERA_JPG)) {
      fi->fh = (uint64_t) (size_t) new SmartPointer<char>(factory.cameras[0].src_camera_jpg.get());
    } else if (cve_isPathSuffix(path, FIREREST_OUTPUT_JPG)) {
      fi->fh = (uint64_t) (size_t) new SmartPointer<char>(factory.cameras[0].src_output_jpg.get());
    } else if (cve_isPathSuffix(path, FIREREST_MONITOR_JPG)) {
      fi->fh = (uint64_t) (size_t) new SmartPointer<char>(factory.cameras[0].src_monitor_jpg.get());
    } else if (cve_isPathSuffix(path, FIREREST_FIRESIGHT_JSON)) {
      result = cve_openVarFile(path, fi);
    } else if (cve_isPathSuffix(path, FIREREST_PROPERTIES_JSON)) {
      result = cve_openVarFile(path, fi);
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
      cve_isPathSuffix(path, FIREREST_SAVED_PNG) ||
      cve_isPathSuffix(path, FIREREST_OUTPUT_JPG)) {
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

int cve_release(const char *path, struct fuse_file_info *fi) {
  LOGTRACE1("cve_release(%s)", path);
  if (cve_isPathSuffix(path, FIREREST_PROCESS_JSON)) {
    //TODO firefuse_freeDataBuffer(path, fi);
  } else if (cve_isPathSuffix(path, FIREREST_MONITOR_JPG)) {
    if (fi->fh) { free( (SmartPointer<char> *) fi->fh); }
  } else if (cve_isPathSuffix(path, FIREREST_SAVED_PNG)) {
    if (fi->fh) { free( (SmartPointer<char> *) fi->fh); }
  } else if (cve_isPathSuffix(path, FIREREST_OUTPUT_JPG)) {
    if (fi->fh) { free( (SmartPointer<char> *) fi->fh); }
  } else if (cve_isPathSuffix(path, FIREREST_FIRESIGHT_JSON)) {
    //TODO firefuse_freeDataBuffer(path, fi);
  } else if (cve_isPathSuffix(path, FIREREST_PROPERTIES_JSON)) {
    //TODO firefuse_freeDataBuffer(path, fi);
  } else if (cve_isPathSuffix(path, FIREREST_CAMERA_JPG)) {
    if (fi->fh) { free( (SmartPointer<char> *) fi->fh); }
  } else if (cve_isPathSuffix(path, FIREREST_SAVE_JSON)) {
    //TODO firefuse_freeDataBuffer(path, fi);
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

bool cve_isPathPrefix(const char *path, const char * prefix) {
  return strncmp(path, prefix, strlen(prefix)) == 0;
}

bool cve_isPathSuffix(const char *value, const char * suffix) {
  int suffixLen = strlen(suffix);
  int valueLen = strlen(value);
  if (suffixLen < valueLen) {
    return strcmp(value + valueLen - suffixLen, suffix) == 0;
  }
  return FALSE;
}

CVE::CVE() {
  const char *firesight = "[{\"op\":\"putText\", \"text\":\"CVE::CVE()\"}]";
  src_firesight_json.post(SmartPointer<char>((char *)firesight, strlen(firesight)+1));
}

CVE::~CVE() {
}
