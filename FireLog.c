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

FILE *logFile = NULL;
int logLevel = LOG_LEVEL_WARN;

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

void firelog(char *fmt, int level, void * value1, void * value2, void * value3) {
  if (logFile) {
    time_t now = time(NULL);
    struct tm *pLocalNow = localtime(&now);
    fprintf(logFile, "%02d:%02d:%02d ", pLocalNow->tm_hour, pLocalNow->tm_min, pLocalNow->tm_sec);
    switch (level) {
      case LOG_LEVEL_ERROR: fprintf(logFile, "[ERROR] "); break;
      case LOG_LEVEL_WARN: fprintf(logFile, "[WARN] "); break;
      case LOG_LEVEL_INFO: fprintf(logFile, "[INFO] "); break;
      case LOG_LEVEL_DEBUG: fprintf(logFile, "[DEBUG] "); break;
      default: fprintf(logFile, "[UNKNOWN%d] ", level); break;
    }
    fprintf(logFile, fmt, value1, value2, value3);
    fprintf(logFile, "\n");
    fflush(logFile);
  }
}
