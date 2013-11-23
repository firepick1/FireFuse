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

#include <sys/stat.h> 
#include <sys/types.h> 
#include <stdlib.h> 
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include "FireStep.h"
#include "FireLog.h"

pthread_t tidReader;

int fdrTinyG = -1;
int fdwTinyG = -1;

#define WRITEBUFMAX 100
#define READBUFMAX 100
#define INBUFMAX 3000
char inbuf[INBUFMAX+1];
int inbuflen = 0;
int inbufEmptyLine = 0;

static void * firestep_reader(void *arg);

int callSystem(char *cmdbuf) {
  int rc = 0;
  rc = system(cmdbuf);
  if (rc == -1) {
    LOGERROR2("callSystem(%s) -> %d", cmdbuf, rc);
    return rc;
  }
  if (WIFEXITED(rc)) {
    if (WEXITSTATUS(rc)) {
      LOGERROR2("callSystem(%s) -> EXIT %d", cmdbuf, WEXITSTATUS(rc));
      return rc;
    }
  } else if (WIFSTOPPED(rc)) {
      LOGERROR2("callSystem(%s) -> STOPPED %d", cmdbuf, WSTOPSIG(rc));
      return rc;
  } else if (WIFSIGNALED(rc)) {
      LOGERROR2("callSystem(%s) -> SIGNALED %d", cmdbuf, WTERMSIG(rc));
      return rc;
  }
  LOGINFO1("callSystem(%s)", cmdbuf);
  return 0;
}

int firestep_init(){
  if (fdrTinyG >= 0) {
    return 0; // already started
  }

  const char * path = "/dev/ttyUSB0";
  char cmdbuf[500];
  sprintf(cmdbuf, "stty 115200 -F %s", path);
  int rc = callSystem(cmdbuf);
  if (rc) {
    return rc;
  }
  sprintf(cmdbuf, 
  	"stty 1400:4:1cb2:a00:3:1c:7f:15:4:1:1:0:11:13:1a:0:12:f:17:16:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0 -F %s", 
	path);
  rc = callSystem(cmdbuf);
  if (rc) {
    return rc;
  }
  fdwTinyG = fdrTinyG = open(path, O_RDWR | O_ASYNC | O_NONBLOCK);
  if (fdrTinyG < 0) {
    rc = errno;
    LOGERROR2("Cannot open %s (errno %d)", path, rc);
    return rc;
  }
  LOGINFO1("firestep_init %s (open) ", path);

  LOGRC(rc, "pthread_create(firestep_reader) -> ", pthread_create(&tidReader, NULL, &firestep_reader, NULL));

  return rc;
}

void firestep_destroy() {
  LOGINFO("firestep_destroy");
  if (fdrTinyG >= 0) {
    close(fdrTinyG);
    fdrTinyG = -1;
  }
}

int firestep_write(const char *buf, size_t bufsize) {
  char message[WRITEBUFMAX+4];
  if (bufsize > WRITEBUFMAX) {
    memcpy(message, buf, WRITEBUFMAX);
    message[WRITEBUFMAX] = '.'; 
    message[WRITEBUFMAX+1] = '.'; 
    message[WRITEBUFMAX+2] = '.'; 
    message[WRITEBUFMAX+3] = 0;
  } else {
    memcpy(message, buf, bufsize);
    message[bufsize] = 0;
  }
  char *s;
  for (s = message; *s; s++) {
    switch (*s) {
      case '\n':
      case '\r':
        *s = ' ';
	break;
    }
  }
  LOGDEBUG1("firestep_write %s start", message);
  ssize_t rc = write(fdwTinyG, buf, bufsize);
  if (rc == bufsize) {
    LOGINFO2("firestep_write %s (%ldB)", message, bufsize);
  } else {
    LOGERROR2("firestep_write %s -> [%ld]", message, rc);
  }
}

static void * firestep_reader(void *arg) {
  char readbuf[READBUFMAX+1];
  LOGINFO("firestep_reader listening...");

  while (fdrTinyG >= 0) {
    int rc = read(fdrTinyG, readbuf, 1);
    if (rc < 0) {
      if (errno == EAGAIN) {
	sched_yield();
	continue;
      }
      LOGERROR2("firestep_reader %s [ERRNO:%d]", inbuf, errno);
      break;
    }
    if (rc == 0) {
      sched_yield();
      continue;
    }
    char c = readbuf[0];
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
      if (inbuflen) { // discard blank lines
	LOGINFO2("firestep_reader %s (%dB)", inbuf, inbuflen);
      } else {
        inbufEmptyLine++;
	if (inbufEmptyLine % 1000 == 0) {
	  LOGWARN1("firestep_reader skipped %ld blank lines", inbufEmptyLine);
	}
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
	LOGTRACE1("firestep_reader %c", (int) c);
      }
    }
  }
  
  LOGINFO("firestep_reader exit");
  return NULL;
}

