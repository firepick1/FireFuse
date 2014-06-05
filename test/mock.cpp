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

int firepicam_create(int a, void *ptr) {
  cameraTime = 0;
  cameraBuffer = NULL;
  return 0;
}

void firepicam_destroy(int count) {
  if (cameraBuffer) {
    free(cameraBuffer);
  }
}

int firepicam_acquireImage(JPG_Buffer & buffer) {
  string path = "../img/headcam";
  if ((cameraTime++ % 2) == 0) {
    path.append("0.jpg");
  } else {
    path.append("1.jpg");
  }
  FILE *fImage = fopen(path.c_str(), "r");
  assert(fImage);
  fseek(fImage, 0, SEEK_END);
  buffer.length = ftell(fImage);
  fseek(fImage, 0, SEEK_SET);
  if (cameraBuffer) {
    free(cameraBuffer);
  }
  cameraBuffer = malloc(length);
  size_t bytesRead = fread(cameraBuffer, 1, length, fImage);
  assert(bytesRead == length);

  return 0;
}
