#include <stdio.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include "version.h"
#include "FireLog.h"
#include "FirePiCam.h"
#include "FireREST.h"
#include "FireSight.hpp"

#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"

using namespace cv;
using namespace firesight;

#define STATUS_BUFFER_SIZE 1024

static char status_buffer[STATUS_BUFFER_SIZE];

double cve_seconds() {
  int64 ticks = getTickCount();
  double ticksPerSecond = getTickFrequency();
  double seconds = ticks/ticksPerSecond;
}

int cve_save(FuseDataBuffer *pBuffer, const char *path) {
  int result = 0;

  char varPath[255];
  snprintf(varPath, sizeof(varPath) - strlen(FIREREST_SAVED_PNG), "%s%s", FIREREST_VAR, path);
  char *pSave = varPath + strlen(varPath) - strlen(FIREREST_SAVE_JSON);
  sprintf(pSave, "%s", FIREREST_SAVED_PNG);

  FILE *fSaved = fopen(varPath, "w");
  if (fSaved) {
    size_t bytes = fwrite(pBuffer->pData, 1, pBuffer->length, fSaved);
    if (bytes == pBuffer->length) {
      LOGTRACE2("cve_save(%s) saved camera image to %s", path, varPath);
    } else {
      LOGERROR2("cve_save(%s) could not write to file: %s", path, varPath);
      result = -EIO;
    }
    fclose(fSaved);
  } else {
    LOGERROR2("cve_save(%s) could not open file for write: %s", path, varPath);
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
