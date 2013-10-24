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
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include "FirePick.h"

long bytes_read = 0;

static void * firefuse_init(struct fuse_conn_info *conn)
{
	bytes_read = 411;
	return 0;
}

static int firefuse_getattr(const char *path, struct stat *stbuf)
{
	int res = 0;

	memset(stbuf, 0, sizeof(struct stat));
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	} else if (strcmp(path, STATUS_PATH) == 0) {
		char *status_str = firepick_status();
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;
		stbuf->st_size = strlen(status_str);
	} else if (strcmp(path, GANTRY1_CAM_PATH) == 0) {
		char *pnpcam_str = firepick_pnpcam();
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;
		stbuf->st_size = strlen(pnpcam_str);
	} else if (strcmp(path, PID_PATH) == 0) {
		char sbuf[20];
		sprintf(sbuf, "%d\n", (int) getpid());
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;
		stbuf->st_size = strlen(sbuf);
	} else if (strcmp(path, TID_PATH) == 0) {
		char sbuf[20];
		sprintf(sbuf, "%ld\n", syscall(SYS_gettid)); //(int) gettid());
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;
		stbuf->st_size = strlen(sbuf);
	} else if (strcmp(path, BYTES_READ_PATH) == 0) {
		char sbuf[20];
		sprintf(sbuf, "%ld\n", bytes_read);
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;
		stbuf->st_size = strlen(sbuf);
	} else
		res = -ENOENT;

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
	filler(buf, GANTRY1_CAM_PATH + 1, NULL, 0);
	filler(buf, PID_PATH + 1, NULL, 0);
	filler(buf, TID_PATH + 1, NULL, 0);
	filler(buf, BYTES_READ_PATH + 1, NULL, 0);

	return 0;
}

static int firefuse_open(const char *path, struct fuse_file_info *fi)
{
	if (strcmp(path, STATUS_PATH) == 0) {
		if ((fi->flags & 3) != O_RDONLY) {
			return -EACCES;
		}
	} else if (strcmp(path, GANTRY1_CAM_PATH) == 0) {
		if ((fi->flags & 3) != O_RDONLY) {
			return -EACCES;
		}
	} else if (strcmp(path, PID_PATH) == 0) {
		if ((fi->flags & 3) != O_RDONLY) {
			return -EACCES;
		}
	} else if (strcmp(path, TID_PATH) == 0) {
		if ((fi->flags & 3) != O_RDONLY) {
			return -EACCES;
		}
	} else if (strcmp(path, BYTES_READ_PATH) == 0) {
		if ((fi->flags & 3) != O_RDONLY) {
			return -EACCES;
		}
	} else {
		return -ENOENT;
	}

	return 0;
}

static int firefuse_release(const char *path, struct fuse_file_info *fi)
{
	return 0;
}

static int firefuse_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	size_t len;
	(void) fi;
	if (strcmp(path, STATUS_PATH) == 0) {
		char *status_str = firepick_status();
		len = strlen(status_str);
		if (offset < len) {
			if (offset + size > len)
				size = len - offset;
			memcpy(buf, status_str + offset, size);
		} else {
			size = 0;
		}
	} else if (strcmp(path, GANTRY1_CAM_PATH) == 0) {
		char *pnpcam_str = firepick_pnpcam();
		len = strlen(pnpcam_str);
		if (offset < len) {
			if (offset + size > len)
				size = len - offset;
			memcpy(buf, pnpcam_str + offset, size);
		} else {
			size = 0;
		}
	} else if (strcmp(path, PID_PATH) == 0) {
		char sbuf[20];
		sprintf(sbuf, "%d\n", (int) getpid());
		len = strlen(sbuf);
		if (offset < len) {
			if (offset + size > len)
				size = len - offset;
			memcpy(buf, sbuf + offset, size);
		} else {
			size = 0;
		}
	} else if (strcmp(path, TID_PATH) == 0) {
		char sbuf[20];
		sprintf(sbuf, "%ld\n", syscall(SYS_gettid)); //(int) gettid());
		len = strlen(sbuf);
		if (offset < len) {
			if (offset + size > len)
				size = len - offset;
			memcpy(buf, sbuf + offset, size);
		} else {
			size = 0;
		}
	} else if (strcmp(path, BYTES_READ_PATH) == 0) {
		char sbuf[20];
		sprintf(sbuf, "%ld\n", bytes_read);
		len = strlen(sbuf);
		if (offset < len) {
			if (offset + size > len)
				size = len - offset;
			memcpy(buf, sbuf + offset, size);
		} else {
			size = 0;
		}
	} else {
		return -ENOENT;
	}

	bytes_read += size;
	return size;
}

static struct fuse_operations firefuse_oper = {
	.init		= firefuse_init,
	.getattr	= firefuse_getattr,
	.readdir	= firefuse_readdir,
	.open		= firefuse_open,
	.release	= firefuse_release,
	.read		= firefuse_read,
};

int main(int argc, char *argv[])
{
	return fuse_main(argc, argv, &firefuse_oper, NULL);
}


