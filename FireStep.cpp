/*
FireStep.cpp https://github.com/firepick1/FirePick/wiki

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

#include "FireStep.h"
#include "FireLog.h"
#include <errno.h>

FILE *fTinyG = NULL;
#define LINEMAX 3000

int firestep_init(){
  int rc = 0;
  const char * path = "/dev/ttyUSB0";
  fTinyG = fopen(path,"ra");
  LOGINFO1("fTinyG %lx", fTinyG);
  if (!fTinyG) {
    rc = errno;
    LOGERROR2("Cannot open %s (errno %d)", path, rc);
    return rc;
  }
  LOGINFO1("firestep_init %s", path);

  return rc;
}

void firestep_destroy() {
  LOGINFO("firestep_destroy");
  if (fTinyG) {
    fclose(fTinyG);
    fTinyG = NULL;
  }
}

void * firestep_thread(void *arg) {
  LOGINFO("firestep_thread listening...");
  for (;fTinyG;) {
    char c = fgetc(fTinyG);
    if (c == EOF && feof(fTinyG)) {
      LOGINFO("TINYG> EOF");
      break;
    } else if (c == NULL) {
      LOGINFO("TINYG> NULL ");
    } else if (c == '\t') {
      LOGINFO("TINYG> \\t ");
    } else if (c == '\r') {
      LOGINFO("TINYG> \\r ");
    } else if (c == '\n') {
      LOGINFO("TINYG> \\n ");
    } else {
      LOGINFO1("TINYG> %c ", (int) c);
    }
  /*
    char line[LINEMAX];
    char *pLine = fgets(line, LINEMAX, fTinyG);
    if (pLine) {
      LOGINFO1("TINYG> %s", pLine);
    } else {
      LOGWARN2("firestep_thread ferror:%d feof:%d", ferror(fTinyG), feof(fTinyG));
    }
    */
  }
  LOGINFO("firestep_thread exit");
  return NULL;
}

