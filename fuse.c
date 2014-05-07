
#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <dirent.h>
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
#include "version.h"
#include "FirePick.h"
#include "FirePiCam.h"
#include "FireStep.h"
#include "FireLog.h"
#include "FireREST.h"

long bytes_read = 0;

FuseDataBuffer headcam_image;     // perpetually changing image
FuseDataBuffer headcam_image_fstat;  // image at time of most recent fstat()

#define FIRELOG_FILE "/var/log/firefuse.log"

char* jpg_pData;
int jpg_length;

#define MAX_ECHO 255
char echoBuf[MAX_ECHO+1];

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

  memset(echoBuf, 0, sizeof(echoBuf));

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

static int getattr_varFile(const char *path, struct stat *stbuf) {
  char varPath[255];
  snprintf(varPath, sizeof(varPath), "%s%s", FIREREST_VAR, path);
  struct stat filestatus;
  int res = stat( varPath, &filestatus );
  if (res) {
    LOGERROR2("getattr_varFile(%s) stat -> %d", path, res);
  } else {
    stbuf->st_mode = S_IFREG | 0444;
    stbuf->st_nlink = 1;
    stbuf->st_size = filestatus.st_size;
  }
  return res;
}

static int firefuse_getattr(const char *path, struct stat *stbuf) {
  int res = 0;

  memset(stbuf, 0, sizeof(struct stat));

  stbuf->st_uid = getuid();
  stbuf->st_gid = getgid();
  stbuf->st_atime = stbuf->st_mtime = stbuf->st_ctime = time(NULL);

  if (strcmp(path, "/") == 0) {
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_nlink = 2;
  } else if (strcmp(path, FIREREST_CV) == 0) {
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_nlink = 1; // Safe default value
  } else if (strcmp(path, FIREREST_CV_1) == 0) {
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_nlink = 1; // Safe default value
  } else if (strcmp(path, FIREREST_CV_1_CVE) == 0) {
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_nlink = 1; // Safe default value
  } else if (strcmp(path, FIREREST_CV_1_MONITOR_JPG) == 0) {
    getattr_varFile(path, stbuf);
  } else if (cve_isPath(path, CVEPATH_CAMERA_LOAD)) {
    stbuf->st_mode = S_IFREG | 0444;
    stbuf->st_nlink = 1;
    memcpy(&headcam_image_fstat, &headcam_image, sizeof(FuseDataBuffer));
    stbuf->st_size = headcam_image_fstat.length;
  } else if (strncmp(path, FIREREST_CV_1_CVE, strlen(FIREREST_CV_1_CVE)) == 0) {
    char * pDot = strchr(path, '.');
    if (pDot) {
      getattr_varFile(path, stbuf);
    } else {
      stbuf->st_mode = S_IFDIR | 0755;
      stbuf->st_nlink = 1; // Safe default value
    }
  } else if (strcmp(path, STATUS_PATH) == 0) {
    const char *status_str = firepick_status();
    stbuf->st_mode = S_IFREG | 0444;
    stbuf->st_nlink = 1;
    stbuf->st_size = strlen(status_str);
  } else if (strcmp(path, HOLES_PATH) == 0) {
    memcpy(&headcam_image_fstat, &headcam_image, sizeof(FuseDataBuffer));
    stbuf->st_mode = S_IFREG | 0666;
    stbuf->st_nlink = 1;
    stbuf->st_size = 1;
    stbuf->st_size = headcam_image_fstat.length;
  } else if (strcmp(path, ECHO_PATH) == 0) {
    stbuf->st_mode = S_IFREG | 0666;
    stbuf->st_nlink = 1;
    stbuf->st_size = strlen(echoBuf);
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

  if (res == -ENOENT) {
    LOGERROR1("firefuse_getattr(%s) ENOENT", path);
  } else if (res) {
    LOGERROR3("firefuse_getattr(%s) st_size:%ldB -> %d", path, (long) stbuf->st_size, res);
  } else {
    LOGTRACE3("firefuse_getattr(%s) st_size:%ldB -> %d OK", path, (long) stbuf->st_size, res);
  }
  return res;
}

static int firefuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
       off_t offset, struct fuse_file_info *fi)
{
  (void) offset;
  (void) fi;

  LOGDEBUG1("firefuse_readdir(%s)", path);

  if (strcmp(path, "/") == 0) {
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    filler(buf, STATUS_PATH + 1, NULL, 0);
    filler(buf, HOLES_PATH + 1, NULL, 0);
    filler(buf, CAM_PATH + 1, NULL, 0);
    filler(buf, FIRELOG_PATH + 1, NULL, 0);
    filler(buf, ECHO_PATH + 1, NULL, 0);
    filler(buf, FIRESTEP_PATH + 1, NULL, 0);
    filler(buf, FIREREST_CV + 1, NULL, 0);
  } else if (strcmp(path, FIREREST_CV) == 0) {
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    filler(buf, FIREREST_1 + 1, NULL, 0);
  } else if (strcmp(path, FIREREST_CV_1) == 0) {
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    filler(buf, FIREREST_CVE + 1, NULL, 0);
    filler(buf, FIREREST_CAMERA_JPG + 1, NULL, 0);
    filler(buf, FIREREST_MONITOR_JPG + 1, NULL, 0);
  } else if (strncmp(path, FIREREST_CV_1_CVE_SUBDIR, strlen(FIREREST_CV_1_CVE_SUBDIR)) == 0) {
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    filler(buf, FIREREST_FIRESIGHT_JSON + 1, NULL, 0);
    filler(buf, FIREREST_OUTPUT_JPG + 1, NULL, 0);
    filler(buf, FIREREST_SAVED_PNG + 1, NULL, 0);
    filler(buf, FIREREST_SAVE_JSON + 1, NULL, 0);
    filler(buf, FIREREST_PROCESS_JSON + 1, NULL, 0);
  } else if (strcmp(path, FIREREST_CV_1_CVE) == 0) {
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    DIR *dirp = opendir(FIREREST_VAR_1_CVE);
    if (dirp) {
      struct dirent * dp;
      while ((dp = readdir(dirp)) != NULL) {
	LOGTRACE1("readdir(%s)", dp->d_name);
	filler(buf, dp->d_name, NULL, 0);
      }
      (void)closedir(dirp);
    } else {
      LOGERROR1("firefuse_readdir(%s) opendir failed", path);
      return -ENOENT;
    }
  } else {
    LOGERROR1("firefuse_readdir(%s) Unknown path", path);
    return -ENOENT;
  }

  return 0;
}

static FuseDataBuffer* firefuse_allocDataBuffer(const char *path, struct fuse_file_info *fi, const char *pData, size_t length) {
  FuseDataBuffer *pBuffer = calloc(sizeof(FuseDataBuffer) + length, 1);
  if (pBuffer) {
    pBuffer->length = length;
    pBuffer->pData = (void *) &pBuffer->reserved; 
    if (pData) {
      memcpy(pBuffer->pData, pData, length);
      LOGTRACE2("firefuse_allocDataBuffer(%s) allocated and initialized %ldB", path, length);
    } else {
      LOGTRACE2("firefuse_allocDataBuffer(%s) allocated %ldB", path, length);
    }
  } else {
    LOGERROR2("firefuse_allocDataBuffer(%s) Could not allocate memory: %ldB", path, length);
  }
  fi->direct_io = 1;
  fi->fh = (uint64_t) (size_t) pBuffer;
  return pBuffer;
}

static FuseDataBuffer* firefuse_allocImage(const char *path, struct fuse_file_info *fi) {
  int length = headcam_image_fstat.length;
  FuseDataBuffer *pJPG = firefuse_allocDataBuffer(path, fi, headcam_image_fstat.pData, length);
  return pJPG;
}

static int firefuse_openFireREST(const char *path, struct fuse_file_info *fi) {
  int result = 0;
  char varPath[255];
  snprintf(varPath, sizeof(varPath), "%s%s", FIREREST_VAR, path);
  FILE *file = fopen(varPath, "rb");
  if (!file) {
    LOGERROR1("firefuse_openFireREST(%s) fopen failed", varPath);
    return -ENOENT;
  }

  fseek(file, 0, SEEK_END);
  size_t length = ftell(file);
  fseek(file, 0, SEEK_SET);
    
  FuseDataBuffer *pBuffer = firefuse_allocDataBuffer(path, fi, NULL, length);
  if (pBuffer) {
    size_t bytesRead = fread(pBuffer->pData, 1, length, file);
    if (bytesRead == length) {
      fi->direct_io = 1;
      fi->fh = (uint64_t) (size_t) pBuffer;
    } else {
      LOGERROR3("firefuse_openFireREST(%s) read failed  (%d != %d)", path, bytesRead, length);
      free(pBuffer);
    }
  } else {
    result = -ENOMEM;
  }
  if (file) {
    fclose(file);
  }
  return result;
}

static inline bool verifyOpenR_(const char *path, struct fuse_file_info *fi, int *pResult) {
  if ((fi->flags & 3) != O_RDONLY) {
    LOGERROR1("verifyOpenR_(%s) EACCESS", path);
    (*pResult) = -EACCES;
  }
  return (*pResult) == 0;
}

static inline bool verifyOpenRW(const char *path, struct fuse_file_info *fi, int *pResult) {
  if ((fi->flags & O_DIRECTORY)) {
    LOGERROR1("verifyOpenRW(%s) EACCESS", path);
    (*pResult) = -EACCES;
  }
  return (*pResult) == 0;
}

static int firefuse_open(const char *path, struct fuse_file_info *fi) {
  int result = 0;
  
  if (strcmp(path, STATUS_PATH) == 0) {
    verifyOpenR_(path, fi, &result);
  } else if (strcmp(path, HOLES_PATH) == 0) {
    verifyOpenR_(path, fi, &result);
    FuseDataBuffer *pJPG = firefuse_allocImage(path, fi);
    if (pJPG) {
      firepick_holes(pJPG);
    } else {
      result = -ENOMEM;
    }
  } else if (cve_isPath(path, CVEPATH_PROCESS_JSON)) {
    if (verifyOpenR_(path, fi, &result)) {
      FuseDataBuffer *pJPG = firefuse_allocImage(path, fi);
      const char * pJson = cve_process(pJPG, path);
      int jsonLen = strlen(pJson);
      fi->fh = (uint64_t) (size_t) pJson;
      free(pJPG);
    }
  } else if (cve_isPath(path, CVEPATH_CAMERA_LOAD)) {
    if (verifyOpenR_(path, fi, &result)) {
      FuseDataBuffer *pJPG = firefuse_allocImage(path, fi);
      if (!pJPG) {
	result = -ENOMEM;
      }
    }
  } else if (strcmp(path, FIREREST_CV_1_MONITOR_JPG) == 0) {
    if (verifyOpenR_(path, fi, &result)) {
      result = firefuse_openFireREST(path, fi);
    }
  } else if (strncmp(path, FIREREST_CV_1_CVE, strlen(FIREREST_CV_1_CVE)) == 0) {
    if (verifyOpenR_(path, fi, &result)) {
      result = firefuse_openFireREST(path, fi);
    }
  } else if (strcmp(path, ECHO_PATH) == 0) {
    verifyOpenRW(path, fi, &result);
  } else if (strcmp(path, FIRELOG_PATH) == 0) {
    verifyOpenRW(path, fi, &result);
  } else if (strcmp(path, FIRESTEP_PATH) == 0) {
    verifyOpenRW(path, fi, &result);
  } else {
    result = -ENOENT;
  }

  switch (-result) {
    case ENOENT:
      LOGERROR1("firefuse_open(%s) error ENOENT", path);
      break;
    case EACCES:
      LOGERROR1("firefuse_open(%s) error EACCES", path);
      break;
    default:
      LOGDEBUG2("firefuse_open(%s) OK flags:%0x", path, fi->flags);
      break;
  }

  return result;
}

static void firefuse_freeDataBuffer(const char *path, struct fuse_file_info *fi) {
  if (fi->fh) {
    FuseDataBuffer *pBuffer = (FuseDataBuffer *)(size_t) fi->fh;
    LOGTRACE2("firefuse_release(%s) freeing data buffer: %ldB", path, pBuffer->length);
    free(pBuffer);
    fi->fh = 0;
  }
}

static int firefuse_release(const char *path, struct fuse_file_info *fi) {
  LOGTRACE1("firefuse_release %s", path);
  if (strcmp(path, STATUS_PATH) == 0) {
    // NOP
  } else if (strcmp(path, HOLES_PATH) == 0) {
    firefuse_freeDataBuffer(path, fi);
  } else if (cve_isPath(path, CVEPATH_CAMERA_LOAD)) {
    firefuse_freeDataBuffer(path, fi);
  } else if (strncmp(path, FIREREST_CV_1_CVE, strlen(FIREREST_CV_1_CVE) == 0)) {
    firefuse_freeDataBuffer(path, fi);
  } else if (strcmp(path, ECHO_PATH) == 0) {
    // NOP
  } else if (strcmp(path, FIRELOG_PATH) == 0) {
    // NOP
  } else if (strcmp(path, FIRESTEP_PATH) == 0) {
    // NOP
  }
  return 0;
}

static inline int load_buffer(char *pDst, const char *pSrc, size_t size, off_t offset, size_t len) {
  size_t sizeOut = size;
  if (offset < len) {
    if (offset + sizeOut > len) {
      sizeOut = len - offset;
    }
    memcpy(pDst, pSrc + offset, sizeOut);
  } else {
    sizeOut = 0;
  }

  return sizeOut;
}

static int firefuse_read(const char *path, char *buf, size_t size, off_t offset,
          struct fuse_file_info *fi)
{
  size_t sizeOut = size;
  size_t len;
  (void) fi;

  if (strcmp(path, STATUS_PATH) == 0) {
    const char *status_str = firepick_status();
    sizeOut = load_buffer(buf, status_str, size, offset, strlen(status_str));
  } else if (cve_isPath(path, CVEPATH_PROCESS_JSON)) {
    const char *pJson = (const char *) (size_t)fi->fh;
    sizeOut = load_buffer(buf, pJson, size, offset, strlen(pJson));
  } else if (cve_isPath(path, CVEPATH_CAMERA_SAVE)) {
    FuseDataBuffer *pBuffer = (FuseDataBuffer *) (size_t) fi->fh;
    if (offset == 0) {
      cve_save(pBuffer, path);
    }
    sizeOut = load_buffer(buf, pBuffer->pData, size, offset, strlen(pBuffer->pData));
  } else if (cve_isPath(path, CVEPATH_CAMERA_LOAD)) {
    FuseDataBuffer *pBuffer = (FuseDataBuffer *) (size_t) fi->fh;
    sizeOut = load_buffer(buf, pBuffer->pData, size, offset, pBuffer->length);
  } else if (strncmp(path, FIREREST_CV_1_CVE, strlen(FIREREST_CV_1_CVE)) == 0) {
    FuseDataBuffer *pBuffer = (FuseDataBuffer *) (size_t) fi->fh;
    sizeOut = load_buffer(buf, pBuffer->pData, size, offset, pBuffer->length);
  } else if (strcmp(path, HOLES_PATH) == 0) {
    const char *holes_str = "holes";
    sizeOut = load_buffer(buf, holes_str, size, offset, strlen(holes_str));
  } else if (strcmp(path, ECHO_PATH) == 0) {
    sizeOut = load_buffer(buf, echoBuf, size, offset, strlen(echoBuf));
  } else if (strcmp(path, FIRELOG_PATH) == 0) {
    char *str = "Actual log is " FIRELOG_FILE "\n";
    sizeOut = load_buffer(buf, str, size, offset, strlen(str));
  } else if (strcmp(path, FIRESTEP_PATH) == 0) {
    const char *json = firestep_json();
    sizeOut = load_buffer(buf, json, size, offset, strlen(json));
  } else {
    LOGERROR2("firefuse_read(%s, %ldB) ENOENT", path, size);
    return -ENOENT;
  }

  LOGTRACE3("firefuse_read(%s, %ldB) -> %ldB", path, size, sizeOut);
  bytes_read += sizeOut;
  return sizeOut;
}

static int firefuse_write(const char *path, const char *buf, size_t bufsize, off_t offset, struct fuse_file_info *fi) {
  if (offset) {
    LOGERROR2("firefuse_write %s -> non-zero offset:%ld", path, (long) offset);
    return EINVAL;
  }
  if (!buf) {
    LOGERROR1("firefuse_write %s -> null buffer", path);
    return EINVAL;
  }
  if (strcmp(path, ECHO_PATH) == 0) {
    if (bufsize > MAX_ECHO) {
      sprintf(echoBuf, "firefuse_write %s -> string too long (%d > %d bytes)", path, bufsize, MAX_ECHO);
      LOGERROR1("%s", echoBuf);
      return EINVAL;
    }
    memcpy(echoBuf, buf, bufsize);
    echoBuf[bufsize] = 0;
    LOGINFO2("firefuse_write %s -> %s", path, echoBuf);
  } else if (strcmp(path, FIRELOG_PATH) == 0) {
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
  } else if (strcmp(path, FIRESTEP_PATH) == 0) {
    firestep_write(buf, bufsize);
  }  

  return bufsize;
}

static int firefuse_truncate(const char *path, off_t size)
{
  (void) size;
  if (strcmp(path, "/") == 0) {
    // directory
  } else if (strcmp(path, FIREREST_CV) == 0) {
    // directory
  } else if (strcmp(path, FIREREST_CV_1) == 0) {
    // directory
  } else if (strcmp(path, FIREREST_CV_1_CVE) == 0) {
    // directory
  } else if (strcmp(path, ECHO_PATH) == 0) {
    // NOP
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
  .init    = firefuse_init,
  .destroy  = firefuse_destroy,
  .getattr  = firefuse_getattr,
  .readdir  = firefuse_readdir,
  .open    = firefuse_open,
  .release  = firefuse_release,
  .read    = firefuse_read,
  .truncate  = firefuse_truncate,
  .write    = firefuse_write,
};

int main(int argc, char *argv[])
{
  return fuse_main(argc, argv, &firefuse_oper, NULL);
}


