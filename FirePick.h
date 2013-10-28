/*
FirePick.h https://github.com/firepick1/FirePick/wiki

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

#include "FireFuse.h"

#ifndef FIREPICK_H
#define FIREPICK_H
#ifdef __cplusplus
extern "C" {
#endif

#define STATUS_PATH "/status"
#define RESULT_PATH "/result"
#define SECONDS_PATH "/seconds"
#define PIDTID_PATH "/pid.tid"
#define BYTES_READ_PATH "/bytes_read"
#define CAM_PATH "/cam.jpg"

char* firepick_status();
char* firepick_pnpcam();

#ifdef __cplusplus
}
#endif
#endif
