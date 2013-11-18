/*
FireLog.h https://github.com/firepick1/FirePick/wiki

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

#ifndef FIRELOG_H
#define FIRELOG_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

#define LOG_LEVEL_ERROR 0
#define LOG_LEVEL_WARN 1
#define LOG_LEVEL_INFO 2
#define LOG_LEVEL_DEBUG 3

#define LOGERROR3(fmt,v1,v2,v3) if (logLevel >= LOG_LEVEL_ERROR) {firelog(fmt, LOG_LEVEL_ERROR, (void *)v1, (void *)v2, (void *)v3);}
#define LOGERROR2(fmt,v1,v2) if (logLevel >= LOG_LEVEL_ERROR) {firelog(fmt, LOG_LEVEL_ERROR, (void *)v1, (void *)v2, fmt);}
#define LOGERROR1(fmt,v1) if (logLevel >= LOG_LEVEL_ERROR) {firelog(fmt, LOG_LEVEL_ERROR, (void *)v1, fmt, fmt);}
#define LOGERROR(fmt) if (logLevel >= LOG_LEVEL_ERROR) {firelog(fmt, LOG_LEVEL_ERROR, fmt, fmt, fmt);}
#define LOGWARN3(fmt,v1,v2,v3) if (logLevel >= LOG_LEVEL_WARN) {firelog(fmt, LOG_LEVEL_WARN, (void *)v1, (void *)v2, (void *)v3);}
#define LOGWARN2(fmt,v1,v2) if (logLevel >= LOG_LEVEL_WARN) {firelog(fmt, LOG_LEVEL_WARN, (void *)v1, (void *)v2, fmt);}
#define LOGWARN1(fmt,v1) if (logLevel >= LOG_LEVEL_WARN) {firelog(fmt, LOG_LEVEL_WARN, (void *)v1, fmt, fmt);}
#define LOGWARN(fmt,v1,v2,v3) if (logLevel >= LOG_LEVEL_WARN) {firelog(fmt, LOG_LEVEL_WARN, fmt, fmt, fmt);}
#define LOGINFO3(fmt,v1,v2,v3) if (logLevel >= LOG_LEVEL_INFO) {firelog(fmt, LOG_LEVEL_INFO, (void *)v1, (void *)v2, (void *)v3);}
#define LOGINFO2(fmt,v1,v2) if (logLevel >= LOG_LEVEL_INFO) {firelog(fmt, LOG_LEVEL_INFO, (void *)v1, (void *)v2, fmt);}
#define LOGINFO1(fmt,v1) if (logLevel >= LOG_LEVEL_INFO) {firelog(fmt, LOG_LEVEL_INFO, (void *)v1, fmt, fmt);}
#define LOGINFO(fmt) if (logLevel >= LOG_LEVEL_INFO) {firelog(fmt, LOG_LEVEL_INFO, fmt, fmt, fmt);}
#define LOGDEBUG3(fmt,v1,v2,v3) if (logLevel >= LOG_LEVEL_DEBUG) {firelog(fmt, LOG_LEVEL_DEBUG, (void *)v1, (void *)v2, (void *)v3);}
#define LOGDEBUG2(fmt,v1,v2) if (logLevel >= LOG_LEVEL_DEBUG) {firelog(fmt, LOG_LEVEL_DEBUG, (void *)v1, (void *)v2, fmt);}
#define LOGDEBUG1(fmt,v1) if (logLevel >= LOG_LEVEL_DEBUG) {firelog(fmt, LOG_LEVEL_DEBUG, (void *)v1, fmt, fmt);}
#define LOGDEBUG(fmt) if (logLevel >= LOG_LEVEL_DEBUG) {firelog(fmt, LOG_LEVEL_DEBUG, fmt, fmt, fmt);}

extern int logLevel;
extern FILE *logFile;

int firelog_init(char *path, int level);
int firelog_destroy();
void firelog(char *fmt, int level, void * value1, void * value2, void * value3);

#ifdef __cplusplus
}
#endif
#endif
