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

#include <errno.h>
#include <pthread.h>
#include "FireStep.h"
#include "FireLog.h"

pthread_t tidReader;
pthread_t tidWriter;

FILE *finTinyG = NULL;
#define INBUFMAX 3000
char inbuf[INBUFMAX+1];
int inbuflen = 0;

static void * firestep_reader(void *arg);

int firestep_init(){
  int rc = 0;
  const char * path = "/dev/ttyUSB0";
  finTinyG = fopen(path,"r");
  if (!finTinyG) {
    rc = errno;
    LOGERROR2("Cannot open %s (errno %d)", path, rc);
    return rc;
  }
  setbuf(finTinyG, NULL); // probably redundant
  LOGINFO1("firestep_init %s", path);

  LOGRC(rc, "pthread_create(firestep_reader) -> ", pthread_create(&tidReader, NULL, &firestep_reader, NULL));

  return rc;
}

void firestep_destroy() {
  LOGINFO("firestep_destroy");
  if (finTinyG) {
    fclose(finTinyG);
    finTinyG = NULL;
  }
}

int firestep_write(char *str) {
  LOGINFO1("firestep_write %s start", str);
  char *s;
  for (s = str; *s; s++) {
    LOGINFO1("firestep_write %c", (long) *s);
    int rc = fputc(*s, finTinyG);
    if (rc != *s) {
      LOGERROR1("firestep_write %d", ferror(finTinyG));
      break;
    }
  }
  //fprintf(finTinyG, "%s", str);
  LOGINFO1("firestep_write %s", str);
}

static void * firestep_reader(void *arg) {
  LOGINFO("firestep_reader listening...");

  while (finTinyG && !feof(finTinyG)) {
    char c = fgetc(finTinyG);
    if (c == EOF) {
      inbuf[inbuflen] = 0;
      inbuflen = 0;
      LOGERROR1("firestep_reader %s[EOF]", inbuf);
      break;
    } else if (c == '\0') {
      inbuf[inbuflen] = 0;
      inbuflen = 0;
      LOGERROR1("firestep_reader %s[NULL]", inbuf);
    } else if (c == '\n') {
      inbuf[inbuflen] = 0;
      if (inbuflen) {
	LOGINFO2("firestep_reader %s %dB", inbuf, inbuflen);
      }
      inbuflen = 0;
    } else if (c == '\r') {
      // skip
    } else {
      if (inbuflen >= INBUFMAX) {
	inbuf[INBUFMAX] = 0;
        LOGERROR1("firestep_reader overflow %s", inbuf);
	break;
      } else {
        inbuf[inbuflen] = c;
        inbuflen++;
	LOGDEBUG1("firestep_reader %c", (int) c);
      }
    }
    if (ferror(finTinyG)) {
      LOGERROR1("firestep_reader ferror:%d", ferror(finTinyG));
      break;
    }
  }
  
  LOGINFO("firestep_reader exit");
  return NULL;
}

