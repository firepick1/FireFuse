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

static string camera_profile(const char * path) {
  const char *s = path;
  const char *pStart = 0;
  const char *pEnd = 0;
  for (int i = 0; i < 4; i++ ) {
    if (*s == 0) {
      break;
    }
    switch (i) {
      case 2: pStart = s; break;
      case 3: pEnd = s; break;
    }
  }
  if (*pEnd == 0) {
    LOGERROR1("camera_profile(%s) -> UNKNOWN", path);
    return "UNKNOWN";
  }

  string result(pStart, pEnd-pStart);
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
  if (cve_isPathSuffix(path, FIREREST_PROCESS_JSON) ||
      cve_isPathSuffix(path, FIREREST_CAMERA_JPG)) {
    memcpy(&headcam_image_fstat, &headcam_image, sizeof(FuseDataBuffer));
    stbuf->st_size = headcam_image_fstat.length;
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

static int cve_openFireREST(const char *path, struct fuse_file_info *fi) {
  int result = 0;
  char varPath[255];
  snprintf(varPath, sizeof(varPath), "%s%s", FIREREST_VAR, path);
  FILE *file = fopen(varPath, "rb");
  if (!file) {
    LOGERROR1("cve_openFireREST(%s) fopen failed", varPath);
    return -ENOENT;
  }

  fseek(file, 0, SEEK_END);
  size_t length = ftell(file);
  fseek(file, 0, SEEK_SET);
    
  FuseDataBuffer *pBuffer = firefuse_allocDataBuffer(path, fi, NULL, length);
  if (pBuffer) {
    size_t bytesRead = fread(pBuffer->pData, 1, length, file);
    if (bytesRead == length) {
      fi->direct_io = 1;
      fi->fh = (uint64_t) (size_t) pBuffer;
    } else {
      LOGERROR3("cve_openFireREST(%s) read failed  (%d != %d)", path, bytesRead, length);
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

int cve_open(const char *path, struct fuse_file_info *fi) {
  int result = 0;
    
  if (verifyOpenR_(path, fi, &result)) {
    if (cve_isPathSuffix(path, FIREREST_PROCESS_JSON)) {
      FuseDataBuffer *pJPG = firefuse_allocImage(path, fi);
      if (pJPG) {
	const char * pJson = cve_process(pJPG, path);
	int jsonLen = strlen(pJson);
	fi->fh = (uint64_t) (size_t) pJson;
	free(pJPG);
      } else {
	result = -ENOMEM;
      }
    } else if (cve_isPathSuffix(path, FIREREST_SAVE_JSON)) {
      FuseDataBuffer *pJPG = firefuse_allocImage(path, fi);
      if (!pJPG) {
	result = -ENOMEM;
      }
    } else if (cve_isPathSuffix(path, FIREREST_CAMERA_JPG)) {
      FuseDataBuffer *pJPG = firefuse_allocImage(path, fi);
      if (!pJPG) {
	result = -ENOMEM;
      }
    } else if (cve_isPathSuffix(path, FIREREST_OUTPUT_JPG)) {
      result = cve_openFireREST(path, fi);
    } else if (cve_isPathSuffix(path, FIREREST_FIRESIGHT_JSON)) {
      result = cve_openFireREST(path, fi);
    } else if (cve_isPathSuffix(path, FIREREST_SAVED_PNG)) {
      result = cve_openFireREST(path, fi);
    } else if (cve_isPathSuffix(path, FIREREST_MONITOR_JPG)) {
      result = cve_openFireREST(path, fi);
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
      LOGDEBUG2("cve_open(%s) OK flags:%0x", path, fi->flags);
      break;
  }

  return result;
}

int cve_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
  size_t sizeOut = size;
  size_t len;
  (void) fi;

  if (cve_isPathSuffix(path, FIREREST_PROCESS_JSON)) {
    const char *pJson = (const char *) (size_t)fi->fh;
    sizeOut = firefuse_readBuffer(buf, pJson, size, offset, strlen(pJson));
  } else if (cve_isPathSuffix(path, FIREREST_SAVE_JSON)) {
    FuseDataBuffer *pBuffer = (FuseDataBuffer *) (size_t) fi->fh;
    if (offset == 0) {
      cve_save(pBuffer, path);
    }
    sizeOut = firefuse_readBuffer(buf, pBuffer->pData, size, offset, strlen(pBuffer->pData));
  } else if (cve_isPathSuffix(path, FIREREST_CAMERA_JPG)) {
    FuseDataBuffer *pBuffer = (FuseDataBuffer *) (size_t) fi->fh;
    sizeOut = firefuse_readBuffer(buf, pBuffer->pData, size, offset, pBuffer->length);
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
    snprintf(pResult, 255, fmt, arg1, arg2);\
  } else {\
    LOGERROR("cve_message() out of memory");\
    pResult = NULL;\
  }\
}

const char * cve_process(FuseDataBuffer *pJPG, const char *path) {
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
    int jsonIndent = 2;
    char *pModelStr = json_dumps(pModel, JSON_PRESERVE_ORDER|JSON_COMPACT|JSON_INDENT(jsonIndent));
    LOGINFO2("cve_process(%s) -> %dB", path, strlen(pModelStr));
    json_decref(pModel);
    string monitorPath = buildVarPath(path, FIREREST_MONITOR_JPG, 4);
    LOGTRACE2("cve_process(%s) MONITOR -> %s", path, monitorPath.c_str());
    imwrite(monitorPath.c_str(), image);
    string outputPath = buildVarPath(path, FIREREST_OUTPUT_JPG);
    LOGTRACE2("cve_process(%s) OUTPUT -> %s", path, outputPath.c_str());
    imwrite(outputPath.c_str(), image);
    return pModelStr;
  } catch (char * ex) {
    const char *fmt = "cve_process(%s) EXCEPTION: %s";
    LOGERROR2(fmt, path, ex);
    if (pModelStr) {
      free(pModelStr);
    }
    char *pResult;
    CVE_MESSAGE(pResult, fmt, path, ex);
    return pResult;
  } catch (...) {
    const char *fmt = "cve_process(%s) UNKNOWN EXCEPTION";
    LOGERROR1(fmt, path);
    if (pModelStr) {
      free(pModelStr);
    }
    char *pResult;
    CVE_MESSAGE(pResult, fmt, path, NULL);
    return pResult;
  }

}

int cve_save(FuseDataBuffer *pJPG, const char *path) {
  int result = 0;

  string savedPath = buildVarPath(path, FIREREST_SAVED_PNG);
  LOGTRACE2("cve_save(%s) saving to %s", path, savedPath.c_str());
  FILE *fSaved = fopen(savedPath.c_str(), "w");
  if (fSaved) {
    bool isColor = strcmp("bgr", camera_profile(path).c_str()) == 0;
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
    if (bytes != expectedBytes) {
      LOGERROR3("cve_save(%s) could not write to file: %s (%d)B", path, savedPath.c_str(), bytes);
      result = -EIO;
    }
  } else {
    LOGERROR2("cve_save(%s) could not open file for write: %s", path, savedPath.c_str());
    result = -ENOENT;
  }

  if (result == 0) {
    snprintf(pJPG->pData, pJPG->length, "{\"camera\":{\"time\":\"%.1f\"}}\n", cve_seconds());
  } else {
    snprintf(pJPG->pData, pJPG->length, 
      "{\"camera\":{\"time\":\"%.1f\"},\"save\":{\"error\":\"Could not save camera image for %s\"}}\n", 
      cve_seconds(), path);
  }

  return result;
}

bool parseArgs(int argc, char *argv[], 
  string &pipelineString, char *&imagePath, char * &outputPath, UIMode &uimode, ArgMap &argMap, bool &isTime, int &jsonIndent) 
{
  char *pipelinePath = NULL;
  uimode = UI_STILL;
  isTime = false;
  firelog_level(FIRELOG_INFO);
 
  if (argc <= 1) {
    return false;
  }

  for (int i = 1; i < argc; i++) {
    if (argv[i][0] == 0) {
      // empty argument
    } else if (strcmp("-opencv",argv[i]) == 0) {
      cout << CV_MAJOR_VERSION << "." << CV_MINOR_VERSION << endl;
      exit(0);
    } else if (strcmp("-p",argv[i]) == 0) {
      if (i+1>=argc) {
        LOGERROR("expected pipeline path after -p");
        exit(-1);
      }
      pipelinePath = argv[++i];
      LOGTRACE1("parseArgs(-p) \"%s\" is JSON pipeline path", pipelinePath);
    } else if (strcmp("-ji",argv[i]) == 0) {
      if (i+1>=argc) {
        LOGERROR("expected JSON indent after -ji");
        exit(-1);
      }
      jsonIndent = atoi(argv[++i]);
      LOGTRACE1("parseArgs(-ji) JSON indent:%d", jsonIndent);
    } else if (strcmp("-o",argv[i]) == 0) {
      if (i+1>=argc) {
        LOGERROR("expected output path after -o");
        exit(-1);
      }
      outputPath = argv[++i];
      LOGTRACE1("parseArgs(-o) \"%s\" is output image path", outputPath);
    } else if (strcmp("-time",argv[i]) == 0) {
      isTime = true;
    } else if (strncmp("-D",argv[i],2) == 0) {
      char * pEq = strchr(argv[i],'=');
      if (!pEq || (pEq-argv[i])<=2) {
        LOGERROR("expected argName=argValue pair after -D");
        exit(-1);
      }
      *pEq = 0;
      char *pName = argv[i] + 2;
      char *pVal = pEq + 1;
      argMap[pName] = pVal;
      LOGTRACE2("parseArgs(-D) argMap[%s]=\"%s\"", pName, pVal );
      *pEq = '=';
    } else if (strcmp("-i",argv[i]) == 0) {
      if (i+1>=argc) {
        LOGERROR("expected image path after -i");
        exit(-1);
      }
      imagePath = argv[++i];
      LOGTRACE1("parseArgs(-i) \"%s\" is input image path", imagePath);
    } else if (strcmp("-video", argv[i]) == 0) {
      uimode = UI_VIDEO;
      LOGTRACE("parseArgs(-video) UI_VIDEO user interface selected");
    } else if (strcmp("-warn", argv[i]) == 0) {
      firelog_level(FIRELOG_WARN);
    } else if (strcmp("-error", argv[i]) == 0) {
      firelog_level(FIRELOG_ERROR);
    } else if (strcmp("-info", argv[i]) == 0) {
      firelog_level(FIRELOG_INFO);
    } else if (strcmp("-debug", argv[i]) == 0) {
      firelog_level(FIRELOG_DEBUG);
    } else if (strcmp("-trace", argv[i]) == 0) {
      firelog_level(FIRELOG_TRACE);
    } else {
      LOGERROR1("unknown firesight argument: '%s'", argv[i]);
      return false;
    }
  }
  if (pipelinePath) {
    LOGTRACE1("Reading pipeline: %s", pipelinePath);
    ifstream ifs(pipelinePath);
    stringstream pipelineStream;
    pipelineStream << ifs.rdbuf();
    pipelineString = pipelineStream.str();
  } else {
    pipelineString = "[{\"op\":\"nop\"}]";
  }
  const char *pJsonPipeline = pipelineString.c_str();
  if (strlen(pJsonPipeline) < 10) {
    LOGERROR1("Invalid pipeline path: %s", pipelinePath);
    exit(-1);
  }
  return true;
}
