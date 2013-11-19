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

#define STATUS_BUFFER_SIZE 1024

char status_buffer[STATUS_BUFFER_SIZE];

char* firepick_status() {
  time_t current_time = time(NULL);
  char timebuf[70];
  strcpy(timebuf, ctime(&current_time));
  timebuf[strlen(timebuf)-1] = 0;

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


