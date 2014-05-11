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

#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"

using namespace cv;
using namespace std;
using namespace firesight;

typedef enum{UI_STILL, UI_VIDEO} UIMode;

typedef class CachedJPG {
  private:
    FuseDataBuffer *pCachedJPG;
    void freeBuffer(const char *path) {
      if (pCachedJPG) {
	LOGTRACE2("CachedJPG::freeBuffer(%s) MEMORY-FREE %ldB", path, pCachedJPG->length);
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
    double output_seconds;
    double monitor_seconds;

    FuseDataBuffer *createOutputJPG(const char *path, int *pResult) {
      FuseDataBuffer *pJPG = NULL;
      if (output_image.rows && output_image.cols) {
	vector<uchar> vJPG;
	imencode(".jpg", output_image, vJPG);
	pJPG = firefuse_allocDataBuffer(path, pResult, (const char*) vJPG.data(), vJPG.size());
	LOGTRACE2("CveCam::createOutputJPG(%s) %ldB", path, pJPG->length);
      } else {
	pJPG = produceCameraJPG(path, pResult);
	LOGTRACE2("CveCam::createOutputJPG(%s) unavailable (using camera image) %ldB", path, pJPG->length);
      }
      return pJPG;
    }

    FuseDataBuffer *createMonitorJPG(const char *path, int *pResult) {
      FuseDataBuffer *pJPG = 
      	cve_seconds() - output_seconds < monitor_seconds ?
	produceOutputJPG(path, pResult) : 
	produceCameraJPG(path, pResult);
      LOGTRACE2("CveCam::createMonitorJPG(%s) %ldB", path, pJPG->length);
      return pJPG;
    }

  public:
    CveCam() {
      output_seconds = 0;
      monitor_seconds = 5;
    }

    void setOutput(Mat value) {
       output_image = value;
       output_seconds = cve_seconds();
    }

    void setMonitorSeconds(double value) {
      monitor_seconds = value;
    }

    double getMonitorSeconds() {
      return monitor_seconds;
    }

    size_t sizeCameraJPG(const char *path, int *pResult) {
      FuseDataBuffer *pJPG = firefuse_allocDataBuffer(path, pResult, headcam_image.pData, headcam_image.length);
      cameraJPG.push(path, pJPG);
      return pJPG ? pJPG->length: 0;
    }

    size_t sizeOutputJPG(const char *path, int *pResult) {
      FuseDataBuffer *pJPG = createOutputJPG(path, pResult);
      return outputJPG.push(path, pJPG);
    }

    size_t sizeMonitorJPG(const char *path, int *pResult) {
      FuseDataBuffer *pJPG = createMonitorJPG(path, pResult);
      return monitorJPG.push(path, pJPG);
    }

    FuseDataBuffer *produceCameraJPG(const char *path, int *pResult) {
      FuseDataBuffer *pJPG = cameraJPG.pop(path);
      if (!pJPG) {
	pJPG = firefuse_allocDataBuffer(path, pResult, headcam_image.pData, headcam_image.length);
      }
      LOGTRACE2("CveCam::produceCameraJPG(%s) %ldB", path, pJPG->length);
      return pJPG;
    }

    FuseDataBuffer *produceOutputJPG(const char *path, int *pResult) {
      FuseDataBuffer *pJPG = outputJPG.pop(path);
      if (!pJPG) {
	pJPG = createOutputJPG(path, pResult);
      }
      LOGTRACE2("CveCam::produceOutputJPG(%s) %ldB", path, pJPG->length);
      return pJPG;
    }

    FuseDataBuffer *produceMonitorJPG(const char *path, int *pResult) {
      FuseDataBuffer *pJPG = monitorJPG.pop(path);
      if (!pJPG) {
        pJPG = createMonitorJPG(path, pResult);
      }
      LOGTRACE2("CveCam::produceMonitorJPG(%s) %ldB", path, pJPG->length);
      return pJPG;
    }
} cveCam[1];

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

int cve_getattr(const char *path, struct stat *stbuf) {
  int res = 0;

  memset(stbuf, 0, sizeof(struct stat));

  string sVarPath = buildVarPath(path, "", 0);
  const char* pVarPath = sVarPath.c_str();
  struct stat filestatus;
  res = stat( pVarPath, &filestatus );
  if (res) {
    LOGERROR3("cve_getattr(%s) stat(%s) -> %d", path, pVarPath, res);
  } else {
    LOGTRACE3("cve_getattr(%s) stat(%s) -> %d", path, pVarPath, res);
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
    cveCam[0].sizeCameraJPG(path, &res); // get current picture but ignore size
    stbuf->st_size = 1; // we don't know the JSON size just yet
  } else if (cve_isPathSuffix(path, FIREREST_SAVE_JSON)) {
    cveCam[0].sizeCameraJPG(path, &res); // get current picture but ignore size
    stbuf->st_size = 1; // we don't know the JSON size just yet
  } else if (cve_isPathSuffix(path, FIREREST_CAMERA_JPG)) {
    stbuf->st_size = cveCam[0].sizeCameraJPG(path, &res);
  } else if (cve_isPathSuffix(path, FIREREST_MONITOR_JPG)) {
    stbuf->st_size = cveCam[0].sizeMonitorJPG(path, &res);
  } else if (cve_isPathSuffix(path, FIREREST_OUTPUT_JPG)) {
    stbuf->st_size = cveCam[0].sizeOutputJPG(path, &res);
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
    
  fi->fh = (uint64_t) (size_t) firefuse_allocDataBuffer(path, &result, NULL, length);
  if (result == 0) {
    FuseDataBuffer *pBuffer = (FuseDataBuffer *)(size_t) fi->fh;
    size_t bytesRead = fread(pBuffer->pData, 1, length, file);
    if (bytesRead != length) {
      LOGERROR3("cve_openVarFile(%s) read failed %d != MEMORY-FREE %d)", path, bytesRead, length);
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

FuseDataBuffer * cve_save(FuseDataBuffer *pJPG, const char *path, int *pResult) {
  if (!pJPG) {
    *pResult = -ENOMEM;
    return NULL;
  }
  double sStart = cve_seconds();
  *pResult = 0;

  string savedPath = buildVarPath(path, FIREREST_SAVED_PNG);
  LOGTRACE2("cve_save(%s) saving to %s", path, savedPath.c_str());
  bool isColor = strcmp("bgr", camera_profile(path).c_str()) == 0;
  FILE *fSaved = fopen(savedPath.c_str(), "w");
  if (fSaved) {
    size_t bytes;
    size_t expectedBytes = pJPG->length;
    if (isColor) {
      expectedBytes = pJPG->length;
      bytes = fwrite(pJPG->pData, 1, expectedBytes, fSaved);
    } else {
      vector<uchar> buff;//buffer for coding
      const uchar * pJPGBytes = (const uchar *) pJPG->pData;
      std::vector<uchar> vJPG (pJPGBytes, pJPGBytes + pJPG->length / sizeof(uchar) );
      LOGTRACE1("cve_save(%s) decode grayscale image", path);
      Mat image = imdecode(vJPG, CV_LOAD_IMAGE_GRAYSCALE); 
      vector<int> param = vector<int>(2);
      param[0]=CV_IMWRITE_PNG_COMPRESSION;
      param[1]=3;//default(3)  0-9.
      imencode(".png",image,buff,param);
      expectedBytes = buff.size();
      bytes = fwrite(buff.data(), 1, expectedBytes, fSaved);
    }
    fclose(fSaved);
    if (bytes == expectedBytes) {
      LOGINFO4("cve_save(%s) %s image saved (%ldB) %0.3fs", path, isColor ? "color" : "gray", bytes, cve_seconds() - sStart);
    } else {
      LOGERROR3("cve_save(%s) could not write to file: %s (%d)B", path, savedPath.c_str(), bytes);
      *pResult = -EIO;
    }
  } else {
    LOGERROR2("cve_save(%s) could not open file for write: %s", path, savedPath.c_str());
    *pResult = -ENOENT;
  }

  LOGTRACE2("cve_save(%s) MEMORY-FREE %ldB", path, pJPG->length);
  FuseDataBuffer *pJSON = pJPG; // json is always smaller than JPG, so just re-use
  if (*pResult == 0) {
    snprintf(pJSON->pData, pJSON->length, "{\"camera\":{\"time\":\"%.1f\"}}\n", cve_seconds());
  } else {
    snprintf(pJSON->pData, pJSON->length, 
      "{\"camera\":{\"time\":\"%.1f\"},\"save\":{\"error\":\"Could not save camera image for %s\"}}\n", 
      cve_seconds(), path);
  }
  pJSON->length = strlen(pJSON->pData);
  LOGTRACE2("cve_save(%s) MEMORY-ALLOC %ldB", path, pJSON->length);

  return pJSON;
}

int cve_open(const char *path, struct fuse_file_info *fi) {
  int result = 0;
    
  if (verifyOpenR_(path, fi, &result)) {
    if (cve_isPathSuffix(path, FIREREST_PROCESS_JSON)) {
      FuseDataBuffer *pJPG = cveCam[0].produceCameraJPG(path, &result);
      if (pJPG) {
	const char * pJson = cve_process(pJPG, path);
	fi->fh = (uint64_t) (size_t) pJson;
	LOGTRACE2("cve_open(%s) MEMORY-FREE %ldB", path, pJPG->length);
	free(pJPG);
      }
    } else if (cve_isPathSuffix(path, FIREREST_SAVE_JSON)) {
      FuseDataBuffer *pJPG = cveCam[0].produceCameraJPG(path, &result);
      fi->fh = (uint64_t) (size_t) cve_save(pJPG, path, &result);
    } else if (cve_isPathSuffix(path, FIREREST_CAMERA_JPG)) {
      fi->fh = (uint64_t) (size_t) cveCam[0].produceCameraJPG(path, &result);
    } else if (cve_isPathSuffix(path, FIREREST_OUTPUT_JPG)) {
      fi->fh = (uint64_t) (size_t) cveCam[0].produceOutputJPG(path, &result);
    } else if (cve_isPathSuffix(path, FIREREST_MONITOR_JPG)) {
      fi->fh = (uint64_t) (size_t) cveCam[0].produceMonitorJPG(path, &result);
    } else if (cve_isPathSuffix(path, FIREREST_FIRESIGHT_JSON)) {
      result = cve_openVarFile(path, fi);
    } else if (cve_isPathSuffix(path, FIREREST_SAVED_PNG)) {
      result = cve_openVarFile(path, fi);
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
      }
      break;
  }

  return result;
}

int cve_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
  size_t sizeOut = size;
  size_t len;
  (void) fi;

  if (cve_isPathSuffix(path, FIREREST_PROCESS_JSON)) {
    const char *pJSON = (const char *) (size_t)fi->fh;
    sizeOut = firefuse_readBuffer(buf, pJSON, size, offset, strlen(pJSON));
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
    char *pJson = (char *)(size_t) fi->fh;
    fi->fh = 0;
    LOGTRACE2("cve_release(%s) MEMORY-FREE %ldB", path, strlen(pJson));
    free(pJson);
  } else if (cve_isPathSuffix(path, FIREREST_MONITOR_JPG)) {
    firefuse_freeDataBuffer(path, fi);
  } else if (cve_isPathSuffix(path, FIREREST_SAVED_PNG)) {
    firefuse_freeDataBuffer(path, fi);
  } else if (cve_isPathSuffix(path, FIREREST_OUTPUT_JPG)) {
    firefuse_freeDataBuffer(path, fi);
  } else if (cve_isPathSuffix(path, FIREREST_FIRESIGHT_JSON)) {
    firefuse_freeDataBuffer(path, fi);
  } else if (cve_isPathSuffix(path, FIREREST_CAMERA_JPG)) {
    firefuse_freeDataBuffer(path, fi);
  } else if (cve_isPathSuffix(path, FIREREST_SAVE_JSON)) {
    firefuse_freeDataBuffer(path, fi);
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
  return false;
}

#define CVE_MESSAGE(pResult,fmt,arg1,arg2) {\
  pResult = (char *)malloc(255);\
  if (pResult) {\
    LOGTRACE2("CVE_MESSAGE(%s) MEMORY-ALLOC %ldB", path, 255);\
    snprintf(pResult, 255, fmt, arg1, arg2);\
  } else {\
    LOGERROR("cve_message() out of memory");\
    pResult = NULL;\
  }\
}

const char * cve_process(FuseDataBuffer *pJPG, const char *path) {
  double sStart = cve_seconds();
  string firesightPath = buildVarPath(path, FIREREST_FIRESIGHT_JSON);
  LOGTRACE2("cve_process(%s) loading JSON: %s", path, firesightPath.c_str());
  char *pModelStr = NULL;
  try {
    Pipeline pipeline(firesightPath.c_str(), Pipeline::PATH);
    const uchar * pJPGBytes = (const uchar *) pJPG->pData;
    std::vector<uchar> vJPG (pJPGBytes, pJPGBytes + pJPG->length / sizeof(uchar) );
    LOGTRACE1("cve_process(%s) decode image", path);
    bool isColor = strcmp("bgr", camera_profile(path).c_str()) == 0;
    Mat image = imdecode(vJPG, isColor ? CV_LOAD_IMAGE_COLOR : CV_LOAD_IMAGE_GRAYSCALE); 
    string savedPath = buildVarPath(path, FIREREST_SAVED_PNG);
    ArgMap argMap;
    argMap["saved"] = savedPath.c_str();
    LOGTRACE1("cve_process(%s) process begin", path);
    json_t *pModel = pipeline.process(image, argMap);
    LOGTRACE1("cve_process(%s) process end", path);
    int jsonIndent = 0;
    char *pModelStr = json_dumps(pModel, JSON_PRESERVE_ORDER|JSON_COMPACT|JSON_INDENT(jsonIndent));
    LOGTRACE2("cve_process(%s) MEMORY-ALLOC %ldB", path, strlen(pModelStr));
    json_decref(pModel);
    cveCam[0].setOutput(image);
    LOGINFO3("cve_process(%s) -> %dB %0.3fs", path, strlen(pModelStr), cve_seconds()-sStart);
    return pModelStr;
  } catch (char * ex) {
    const char *fmt = "cve_process(%s) EXCEPTION: %s";
    LOGERROR2(fmt, path, ex);
    if (pModelStr) {
      LOGTRACE2("cve_process(%s) MEMORY-FREE %ldB", path, strlen(pModelStr));
      free(pModelStr);
    }
    char *pResult;
    CVE_MESSAGE(pResult, fmt, path, ex);
    return pResult;
  } catch (...) {
    const char *fmt = "cve_process(%s) UNKNOWN EXCEPTION";
    LOGERROR1(fmt, path);
    if (pModelStr) {
      LOGTRACE2("cve_process(%s) MEMORY-FREE %ldB", path, strlen(pModelStr));
      free(pModelStr);
    }
    char *pResult;
    CVE_MESSAGE(pResult, fmt, path, NULL);
    return pResult;
  }
}

