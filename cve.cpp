#include <string.h>
#include <errno.h>
#include <fcntl.h>
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

double cve_seconds() {
  int64 ticks = getTickCount();
  double ticksPerSecond = getTickFrequency();
  double seconds = ticks/ticksPerSecond;
}

static bool strcmp_suffix(const char *value, const char * suffix) {
  int suffixLen = strlen(suffix);
  int valueLen = strlen(value);
  if (suffixLen < valueLen) {
    return strcmp(value + valueLen - suffixLen, suffix) == 0;
  }
  return false;
}

bool cve_isPath(const char *path, int flags) {
  bool result = 0;
  if (strcmp_suffix(path, FIREREST_SAVE_JSON)) {
    result = (flags & (CVEPATH_CAMERA_LOAD | CVEPATH_CAMERA_SAVE)) ? true : false;
  } else if (strcmp(path, CAM_PATH) == 0) {
    result = (flags & CVEPATH_CAMERA_LOAD) ? true : false;
  } else if (strcmp(path, FIREREST_CV_1_IMAGE_JPG) == 0) {
    result = (flags & CVEPATH_CAMERA_LOAD) ? true : false;
  } else if (strcmp_suffix(path, FIREREST_PROCESS_JSON)) {
    result = (flags & CVEPATH_PROCESS_JSON) ? true : false;
  }
  LOGTRACE3("cve_isPath(%s, 0x%x) -> %d", path, flags, result);
  return result;
}

string buildVarPath(const char * path, const char *fName, int parent=1) {
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
    Mat image = imdecode(vJPG, CV_LOAD_IMAGE_COLOR); 
    string savedPath = buildVarPath(path, FIREREST_SAVED_PNG);
    ArgMap argMap;
    argMap["saved"] = savedPath.c_str();
    json_t *pModel = pipeline.process(image, argMap);
    int jsonIndent = 2;
    char *pModelStr = json_dumps(pModel, JSON_PRESERVE_ORDER|JSON_COMPACT|JSON_INDENT(jsonIndent));
    LOGDEBUG2("cve_process(%s) -> %s", path, pModelStr);
    json_decref(pModel);
    string monitorPath = buildVarPath(path, FIREREST_MONITOR_JPG, 3);
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

int cve_save(FuseDataBuffer *pBuffer, const char *path) {
  int result = 0;

  string savedPath = buildVarPath(path, FIREREST_SAVED_PNG);
  LOGTRACE2("cve_save(%s) saving to %s", path, savedPath.c_str());
  FILE *fSaved = fopen(savedPath.c_str(), "w");
  if (fSaved) {
    size_t bytes = fwrite(pBuffer->pData, 1, pBuffer->length, fSaved);
    if (bytes != pBuffer->length) {
      LOGERROR2("cve_save(%s) could not write to file: %s", path, savedPath.c_str());
      result = -EIO;
    }
    fclose(fSaved);
  } else {
    LOGERROR2("cve_save(%s) could not open file for write: %s", path, savedPath.c_str());
    result = -ENOENT;
  }

  if (result == 0) {
    snprintf(pBuffer->pData, pBuffer->length, "{\"camera\":{\"time\":\"%.1f\"}}\n", cve_seconds());
  } else {
    snprintf(pBuffer->pData, pBuffer->length, 
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
