/*
FireFuse.c https://github.com/firepick1/FirePick/wiki

Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
Copyright (C) 2013  Karl Lew, <karl@firepick.org>, changes adapted from hello.c by Miklos Szeredi

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

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include "FirePick.h"
#include "FirePiCam.h"
#include "FireStep.h"
#include "FireLog.h"

long bytes_read = 0;
long seconds = 0;

JPG headcam_image; 		// perpetually changing image
JPG headcam_image_fstat;	// image at time of most recent fstat()

#define FIRELOG_FILE "/var/log/firefuse.log"

char* jpg_pData;
int jpg_length;

/////////////////////// THREADS ////////////////////////

pthread_t tidCamera;

static void * firefuse_cameraThread(void *arg) {
  LOGINFO("firefuse_cameraThread start");
  firepick_camera_daemon(&headcam_image);
  return NULL;
}

/////////////////////// FIREFUSE CALLBACKS //////////////////////

static void * firefuse_init(struct fuse_conn_info *conn)
{
  int rc = 0;

  firelog_init(FIRELOG_FILE, FIRELOG_INFO);
  LOGINFO2("Initialized FireFuse %d.%d", FireFuse_VERSION_MAJOR, FireFuse_VERSION_MINOR);
  LOGINFO2("PID%d UID%d", (int) getpid(), (int)getuid());

  headcam_image.pData = NULL;
  headcam_image.length = 0;
  headcam_image_fstat.pData = NULL;
  headcam_image_fstat.length = 0;

  LOGRC(rc, "pthread_create(&tidCamera...) -> ", pthread_create(&tidCamera, NULL, &firefuse_cameraThread, NULL));

  firestep_init();

  return NULL; /* init */
}

static void firefuse_destroy(void * initData)
{
	if (logFile) {
	  LOGINFO("firefuse_destroy()");
	  firelog_destroy();
	}
}

static int firefuse_getattr(const char *path, struct stat *stbuf)
{
  int res = 0;

  memset(stbuf, 0, sizeof(struct stat));

  stbuf->st_uid = getuid();
  stbuf->st_gid = getgid();
  stbuf->st_atime = stbuf->st_mtime = stbuf->st_ctime = time(NULL);

  if (strcmp(path, "/") == 0) {
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_nlink = 2;
  } else if (strcmp(path, STATUS_PATH) == 0) {
    const char *status_str = firepick_status();
    stbuf->st_mode = S_IFREG | 0444;
    stbuf->st_nlink = 1;
    stbuf->st_size = strlen(status_str);
  } else if (strcmp(path, CIRCLES_PATH) == 0) {
    memcpy(&headcam_image_fstat, &headcam_image, sizeof(JPG));
    stbuf->st_mode = S_IFREG | 0444;
    stbuf->st_nlink = 1;
    stbuf->st_size = 1;
    stbuf->st_size = headcam_image_fstat.length;
  } else if (strcmp(path, CAM_PATH) == 0) {
    memcpy(&headcam_image_fstat, &headcam_image, sizeof(JPG));
    stbuf->st_mode = S_IFREG | 0444;
    stbuf->st_nlink = 1;
    stbuf->st_size = headcam_image_fstat.length;
  } else if (strcmp(path, FIRELOG_PATH) == 0) {
    stbuf->st_mode = S_IFREG | 0666;
    stbuf->st_nlink = 1;
    stbuf->st_size = 10000;
  } else if (strcmp(path, FIRESTEP_PATH) == 0) {
    stbuf->st_mode = S_IFREG | 0666;
    stbuf->st_nlink = 1;
    stbuf->st_size = strlen(firestep_json());
  } else {
    res = -ENOENT;
  }

  if (res) {
    LOGERROR3("firefuse_getattr %s st_size:%ldB -> %d", path, (long) stbuf->st_size, res);
  } else {
    LOGDEBUG3("firefuse_getattr %s st_size:%ldB -> %d", path, (long) stbuf->st_size, res);
  }
  return res;
}

static int firefuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi)
{
  (void) offset;
  (void) fi;

  if (strcmp(path, "/") != 0)
	  return -ENOENT;

  filler(buf, ".", NULL, 0);
  filler(buf, "..", NULL, 0);
  filler(buf, STATUS_PATH + 1, NULL, 0);
  filler(buf, CIRCLES_PATH + 1, NULL, 0);
  filler(buf, CAM_PATH + 1, NULL, 0);
  filler(buf, FIRELOG_PATH + 1, NULL, 0);
  filler(buf, FIRESTEP_PATH + 1, NULL, 0);

  return 0;
}

static JPG* firefuse_allocImage(const char *path, struct fuse_file_info *fi) {
  int result = 0;
	int length = headcam_image_fstat.length;
	JPG *pJPG = calloc(sizeof(JPG) + length, 1);
	if (pJPG) {
		pJPG->length = length;
		pJPG->pData = (void *) &pJPG->reserved; 
		memcpy(pJPG->pData, headcam_image_fstat.pData, length);
		LOGTRACE1("firefuse_allocImage allocated image memory: %ldB", length);
	} else {
		LOGERROR2("firefuse_allocImage %s Could not allocate image memory: %ldB", path, length);
		result = -ENOMEM;
	}
	fi->direct_io = 1;
	//fi->nonseekable = 1;
	fi->fh = (uint64_t) (size_t) pJPG;
	return pJPG;
}

static int firefuse_open(const char *path, struct fuse_file_info *fi) {
	int result = 0;
	
  if (strcmp(path, STATUS_PATH) == 0) {
    if ((fi->flags & 3) != O_RDONLY) {
	    return -EACCES;
    }
  } else if (strcmp(path, CIRCLES_PATH) == 0) {
    if ((fi->flags & 3) != O_RDONLY) {
	    return -EACCES;
    }
		JPG *pJPG = firefuse_allocImage(path, fi);
		if (!pJPG) {
			return -ENOMEM;
		}
		firepick_circles(pJPG);
  } else if (strcmp(path, CAM_PATH) == 0) {
    if ((fi->flags & 3) != O_RDONLY) {
	    return -EACCES;
    }
		JPG *pJPG = firefuse_allocImage(path, fi);
		if (!pJPG) {
			return -ENOMEM;
		}
  } else if (strcmp(path, FIRELOG_PATH) == 0) {
    if ((fi->flags & O_DIRECTORY)) {
      LOGERROR1("firefuse_open %s -> O_DIRECTORY not allowed ", path);
      return -EACCES;
    }
    LOGDEBUG2("firefuse_open %s %0x", path, fi->flags);
  } else if (strcmp(path, FIRESTEP_PATH) == 0) {
    if ((fi->flags & O_DIRECTORY)) {
      LOGERROR1("firefuse_open %s -> O_DIRECTORY not allowed ", path);
      return -EACCES;
    }

    LOGDEBUG2("firefuse_open %s %0x", path, fi->flags);
  } else {
    LOGERROR1("firefuse_open Unknown path %s", path);
    return -ENOENT;
  }

  return result;
}

static void firefuse_freeImage(const char *path, struct fuse_file_info *fi) {
	if (fi->fh) {
		JPG *pJPG = (JPG *)(size_t) fi->fh;
		LOGTRACE2("firefuse_release %s freeing image: %ldB", path, pJPG->length);
		free(pJPG);
		fi->fh = 0;
  }
}

static int firefuse_release(const char *path, struct fuse_file_info *fi) {
  LOGTRACE1("firefuse_release %s", path);
  if (strcmp(path, STATUS_PATH) == 0) {
    // NOP
  } else if (strcmp(path, CIRCLES_PATH) == 0) {
		firefuse_freeImage(path, fi);
  } else if (strcmp(path, FIRELOG_PATH) == 0) {
    // NOP
  } else if (strcmp(path, FIRESTEP_PATH) == 0) {
    // NOP
  } else if (strcmp(path, CAM_PATH) == 0) {
		firefuse_freeImage(path, fi);
  }
  return 0;
}

static int firefuse_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
  size_t len;
  (void) fi;
	LOGTRACE2("firefuse_read(%s, %ldB)", path, size);

  if (strcmp(path, STATUS_PATH) == 0) {
    const char *status_str = firepick_status();
    len = strlen(status_str);
    if (offset < len) {
	    if (offset + size > len)
		    size = len - offset;
	    memcpy(buf, status_str + offset, size);
    } else {
	    size = 0;
    }
  } else if (strcmp(path, CIRCLES_PATH) == 0) {
    const char *circles_str = "circles";
    len = strlen(circles_str);
    if (offset < len) {
	    if (offset + size > len)
		    size = len - offset;
	    memcpy(buf, circles_str + offset, size);
    } else {
	    size = 0;
    }
  } else if (strcmp(path, CAM_PATH) == 0) {
    JPG *pJPG = (JPG *) (size_t) fi->fh;
    len = pJPG->length;
    if (offset < len) {
      if (offset + size > len) {
				size = len - offset;
      }
      memcpy(buf, pJPG->pData + offset, size);
    } else {
      size = 0;
    }
  } else if (strcmp(path, FIRELOG_PATH) == 0) {
	  char *str = "Actual log is " FIRELOG_FILE "\n";
    len = strlen(str);
    if (offset < len) {
	    if (offset + size > len)
		    size = len - offset;
	    memcpy(buf, str + offset, size);
    } else {
	    size = 0;
    }
  } else if (strcmp(path, FIRESTEP_PATH) == 0) {
		const char *json = firestep_json();
    len = strlen(json);
    if (offset < len) {
      if (offset + size > len) {
				size = len - offset;
      }
      memcpy(buf, json + offset, size);
			size = len;
    } else {
      size = 0;
    }
  } else {
    return -ENOENT;
  }

  bytes_read += size;
  return size;
}

static int firefuse_write(const char *pathname, const char *buf, size_t bufsize, off_t offset, struct fuse_file_info *fi)
{
  if (offset) {
    LOGERROR2("firefuse_write %s -> non-zero offset:%ld", pathname, (long) offset);
    return EINVAL;
  }
  if (!buf) {
    LOGERROR1("firefuse_write %s -> null buffer", pathname);
    return EINVAL;
  }
  if (strcmp(pathname, FIRELOG_PATH) == 0) {
    switch (buf[0]) {
      case 'E': 
      case 'e': 
      case '0': 
      	firelog_level(FIRELOG_ERROR); 
	break;
      case 'W': 
      case 'w': 
      case '1': 
      	firelog_level(FIRELOG_WARN); 
	break;
      case 'I': 
      case 'i': 
      case '2': 
      	firelog_level(FIRELOG_INFO); 
      	break;
      case 'D': 
      case 'd': 
      case '3': 
      	firelog_level(FIRELOG_DEBUG); 
	break;
      case 'T': 
      case 't': 
      case '4': 
      	firelog_level(FIRELOG_TRACE); 
	break;
    }
  } else if (strcmp(pathname, FIRESTEP_PATH) == 0) {
    firestep_write(buf, bufsize);
  }  

  return bufsize;
}

static int firefuse_truncate(const char *path, off_t size)
{
  (void) size;
  if (strcmp(path, "/") == 0) {
    // directory
  } else if (strcmp(path, FIRELOG_PATH) == 0) {
    // NOP
  } else if (strcmp(path, FIRESTEP_PATH) == 0) {
    // NOP
  } else {
    return -ENOENT;
  }
  LOGDEBUG1("firefuse_truncate %s", path);
  return 0;
}


static struct fuse_operations firefuse_oper = {
	.init		= firefuse_init,
	.destroy	= firefuse_destroy,
	.getattr	= firefuse_getattr,
	.readdir	= firefuse_readdir,
	.open		= firefuse_open,
	.release	= firefuse_release,
	.read		= firefuse_read,
	.truncate	= firefuse_truncate,
	.write		= firefuse_write,
};

int main(int argc, char *argv[])
{
  return fuse_main(argc, argv, &firefuse_oper, NULL);
}


