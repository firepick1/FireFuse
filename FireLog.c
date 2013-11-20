/*
FireLog.c https://github.com/firepick1/FirePick/wiki

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

#include "FireLog.h"

#include <errno.h>
#include <time.h>
#include <string.h>
#include <sys/syscall.h>

FILE *logFile = NULL;
int logLevel = FIRELOG_WARN;

int firelog_init(char *path, int level) {
  logLevel = level;
  logFile = fopen(path, "w");
  if (!logFile) {
    return errno;
  }
  LOGINFO1("FireLog %s", path);
  return 0;
}

int firelog_destroy() {
  int rc = 0;
  if (logFile) {
    rc = fclose(logFile);
  }
  return rc;
}

int firelog_level(int newLevel) {
  int oldLevel = logLevel;
  logLevel = newLevel;
  LOGINFO1("firelog_level(%d)", newLevel);
  return oldLevel;
}

void firelog(const char *fmt, int level, const void * value1, const void * value2, const void * value3) {
  if (logFile) {
    time_t now = time(NULL);
    struct tm *pLocalNow = localtime(&now);
    int tid = syscall(SYS_gettid);
    fprintf(logFile, "%02d:%02d:%02d ", pLocalNow->tm_hour, pLocalNow->tm_min, pLocalNow->tm_sec);
    switch (level) {
      case FIRELOG_ERROR: fprintf(logFile, "ERROR %d ", tid); break;
      case FIRELOG_WARN: fprintf(logFile, "WARN %d ", tid); break;
      case FIRELOG_INFO: fprintf(logFile, "INFO %d ", tid); break;
      case FIRELOG_DEBUG: fprintf(logFile, "DEBUG %d ", tid); break;
      case FIRELOG_TRACE: fprintf(logFile, "TRACE %d ", tid); break;
      default: fprintf(logFile, "?%d? %d ", level, tid); break;
    }
    fprintf(logFile, fmt, value1, value2, value3);
    fprintf(logFile, "\n", tid);
    fflush(logFile);
  }
}
