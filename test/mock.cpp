#include "FireSight.hpp"
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>
#include <time.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include "firefuse.h"
#include "version.h"
#include "FirePiCam.h"

#define CAMERA_BUF_SIZE 1600000
int cameraTime;
char *cameraBuffer;


int firepicam_create(int argc, const char **argv) {
  cameraTime = 0;
  cameraBuffer = NULL;
  cout << "firepicam_create() ";
  for (int i = 0; i < argc; i++) {
    cout << argv[i] << " ";
  }
  cout << endl;
  return 0;
}

int firepicam_destroy(int status) {
  if (cameraBuffer) {
    free(cameraBuffer);
  }
  return 0;
}

int firepicam_acquireImage(JPG_Buffer *pBuffer) {
  string path = "headcam";
  if ((cameraTime++ % 2) == 0) {
    path.append("0.jpg");
  } else {
    path.append("1.jpg");
  }
  FILE *fImage = fopen(path.c_str(), "r");
  if (fImage) {
    LOGINFO1("firepicam_acquireImage(%s)", path.c_str());
  } else {
    path.insert(0, "test/"); 
    LOGINFO1("firepicam_acquireImage(%s)", path.c_str());
    fImage = fopen(path.c_str(), "r");
  }
  assert(fImage);
  fseek(fImage, 0, SEEK_END);
  pBuffer->length = ftell(fImage);
  fseek(fImage, 0, SEEK_SET);
  if (cameraBuffer) {
    free(cameraBuffer);
  }
  cameraBuffer = (char *) malloc(pBuffer->length);
  pBuffer->pData = cameraBuffer;
  size_t bytesRead = fread(cameraBuffer, 1, pBuffer->length, fImage);
  assert(bytesRead == pBuffer->length);

  return 0;
}

void firepicam_print_elapsed() {
  LOGINFO("firepicam_print_elapsed()");
}
