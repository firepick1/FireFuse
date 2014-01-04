/*
FirePick.cpp https://github.com/firepick1/FirePick/wiki

Copyright (C) 2013  Karl Lew, <karl@firepick.org>

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <stdio.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include "FirePick.h"
#include "FireLog.h"
#include "FirePiCam.h"
#include "FireSight.hpp"

#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"

#define STATUS_BUFFER_SIZE 1024

char status_buffer[STATUS_BUFFER_SIZE];

const void* firepick_circles(JPG *pJPG) {
	cv::Mat jpg(1, pJPG->length, CV_8UC1, pJPG->pData);
	cv::Mat matRGB = imdecode(jpg, CV_LOAD_IMAGE_COLOR);

	MSER_holes recognizer(26);
  recognizer.scan(matRGB);

	imwrite("/home/pi/camcv.bmp", matRGB);

  return pJPG;
}

const char* firepick_status() {
  time_t current_time = time(NULL);
  char timebuf[70];
  strcpy(timebuf, ctime(&current_time));
  timebuf[strlen(timebuf)-1] = 0;

	const char *errorOrWarn = firelog_lastMessage(FIRELOG_WARN);
	if (strlen(errorOrWarn)) {
		return errorOrWarn;
	}

  sprintf(status_buffer, 
    "{\n"
    " 'timestamp':'%s'\n"
    " 'message':'FirePick OK!',\n"
    " 'version':'FireFuse version %d.%d'\n"
    "}\n",
    timebuf,
    FireFuse_VERSION_MAJOR, FireFuse_VERSION_MINOR);
  return status_buffer;
}

int firepick_camera_daemon(JPG *pJPG) {
  int status = firepicam_create(0, NULL);

  LOGINFO1("firepick_camera_daemon start -> %d", status);

  for (;;) {
    JPG_Buffer buffer;
    buffer.pData = NULL;
    buffer.length = 0;
    
    status = firepicam_acquireImage(&buffer);
    pJPG->pData = buffer.pData;
    pJPG->length = buffer.length;
  }

  LOGINFO1("firepick_camera_daemon exit -> %d", status);
  firepicam_destroy(status);
}


