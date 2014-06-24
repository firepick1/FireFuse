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
#include "FirePiCam.h"
#include "FireLog.h"
#include "firefuse.h"

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
  background_worker();
  return NULL;
}

/////////////////////// FIREFUSE CALLBACKS //////////////////////
static char *pConfigJson = NULL;

#define CONFIG_JSON "/var/firefuse/config.json"

static void * firefuse_init(struct fuse_conn_info *conn) {
  int rc = 0;

  firelog_init(FIRELOG_FILE, FIRELOG_INFO);
  //firelog_init(FIRELOG_FILE, FIRELOG_TRACE);
  LOGINFO4("FireFUSE %d.%d.%d fuse_root:%s", FireFUSE_VERSION_MAJOR, FireFUSE_VERSION_MINOR, FireFUSE_VERSION_PATCH, fuse_root);
  LOGINFO2("PID%d UID%d", (int) getpid(), (int)getuid());

  LOGINFO1("Loading FireREST configuration: %s", CONFIG_JSON);
  FILE *fConfig = fopen(CONFIG_JSON, "r");
  if (fConfig == 0) {
    LOGERROR1("FATAL: Could not open configuration file: %s", CONFIG_JSON);
    exit(-ENOENT);
  }
  fseek(fConfig, 0, SEEK_END);
  size_t length = ftell(fConfig);
  fseek(fConfig, 0, SEEK_SET);
  pConfigJson = malloc(length + 1);
  size_t bytesRead = fread(pConfigJson, 1, length, fConfig);
  if (bytesRead != length) {
    LOGERROR2("FATAL: Could not read configuration file: expected:%ldB actual:%ldB", length, bytesRead);
    exit(-EIO);
  }
  pConfigJson[length] = 0;
  fclose(fConfig);
  firerest_config(pConfigJson);

  memset(echoBuf, 0, sizeof(echoBuf));

  headcam_image.pData = NULL;
  headcam_image.length = 0;
  headcam_image_fstat.pData = NULL;
  headcam_image_fstat.length = 0;

  LOGRC(rc, "pthread_create(&tidCamera...) -> ", pthread_create(&tidCamera, NULL, &firefuse_cameraThread, NULL));

  firestep_init();

  return NULL; /* init */
}

static void firefuse_destroy(void * initData) {
  if (logFile) {
    LOGINFO("firefuse_destroy()");
    firelog_destroy();
  }
  if (pConfigJson) {
    free(pConfigJson);
  }
}

static int firefuse_getattr(const char *path, struct stat *stbuf) {
  if (is_cv_path(path)) {
    return cve_getattr(path, stbuf);
  }
  if (is_cnc_path(path)) {
    return cnc_getattr(path, stbuf);
  }

  int res = 0;
  struct stat st; 

  memset(stbuf, 0, sizeof(struct stat));

  stbuf->st_uid = getuid();
  stbuf->st_gid = getgid();
  stbuf->st_atime = stbuf->st_mtime = stbuf->st_ctime = time(NULL);

  if (strcmp(path, "/") == 0) {
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_nlink = 2;
    stbuf->st_nlink = 1; // Safe default value
  } else if (strcmp(path, CONFIG_PATH) == 0) {
    stbuf->st_mode = S_IFREG | 0444;
    stbuf->st_nlink = 1;
    stbuf->st_size = strlen(pConfigJson);
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
  if (is_cv_path(path)) {
    return cve_readdir(path, buf, filler, offset, fi);
  }

  (void) offset;
  (void) fi;

  LOGDEBUG1("firefuse_readdir(%s)", path);

  if (strcmp(path, "/") == 0) {
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    filler(buf, STATUS_PATH + 1, NULL, 0);
    filler(buf, CONFIG_PATH + 1, NULL, 0);
    filler(buf, HOLES_PATH + 1, NULL, 0);
    filler(buf, FIRELOG_PATH + 1, NULL, 0);
    filler(buf, ECHO_PATH + 1, NULL, 0);
    filler(buf, FIRESTEP_PATH + 1, NULL, 0);
    filler(buf, FIREREST_CV + 1, NULL, 0);
    filler(buf, FIREREST_SYNC + 1, NULL, 0);
  } else {
    LOGERROR1("firefuse_readdir(%s) Unknown path", path);
    return -ENOENT;
  }

  return 0;
}

static int firefuse_open(const char *path, struct fuse_file_info *fi) {
  if (is_cv_path(path)) {
    return cve_open(path, fi);
  }

  int result = 0;
  
  if (strcmp(path, STATUS_PATH) == 0) {
    verifyOpenR_(path, fi, &result);
  } else if (strcmp(path, CONFIG_PATH) == 0) {
    verifyOpenR_(path, fi, &result);
  } else if (strcmp(path, HOLES_PATH) == 0) {
    verifyOpenR_(path, fi, &result);
    fi->fh = (uint64_t) (size_t) fopen("/var/firefuse/config.json", "r");
    if (!fi->fh) {
      result = -ENOENT;
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
      if (fi->fh) {
	fi->direct_io = 1;
      }
      LOGDEBUG2("firefuse_open(%s) OK flags:%0x", path, fi->flags);
      break;
  }

  return result;
}

void firefuse_freeDataBuffer(const char *path, struct fuse_file_info *fi) {
  if (fi->fh) {
    FuseDataBuffer *pBuffer = (FuseDataBuffer *)(size_t) fi->fh;
    LOGTRACE2("firefuse_freeDataBuffer(%s) MEMORY-FREE: %ldB", path, (long) pBuffer->length);
    free(pBuffer);
    fi->fh = 0;
  }
}

static int firefuse_release(const char *path, struct fuse_file_info *fi) {
  if (is_cv_path(path)) {
    return cve_release(path, fi);
  }

  LOGTRACE1("firefuse_release %s", path);
  if (strcmp(path, STATUS_PATH) == 0) {
    // NOP
  } else if (strcmp(path, CONFIG_PATH) == 0) {
    // NOP
  } else if (strcmp(path, HOLES_PATH) == 0) {
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

static int firefuse_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
  if (is_cv_path(path)) {
    int res = cve_read(path, buf, size, offset, fi);
    if (res > 0) {
      bytes_read += res;
    }
    return res;
  }

  size_t sizeOut = size;
  size_t len;
  (void) fi;

  if (strcmp(path, STATUS_PATH) == 0) {
    const char *status_str = firepick_status();
    sizeOut = firefuse_readBuffer(buf, status_str, size, offset, strlen(status_str));
  } else if (strcmp(path, CONFIG_PATH) == 0) {
    sizeOut = firefuse_readBuffer(buf, pConfigJson, size, offset, strlen(pConfigJson));
  } else if (strcmp(path, HOLES_PATH) == 0) {
    const char *holes_str = "holes";
    sizeOut = firefuse_readBuffer(buf, holes_str, size, offset, strlen(holes_str));
  } else if (strcmp(path, ECHO_PATH) == 0) {
    sizeOut = firefuse_readBuffer(buf, echoBuf, size, offset, strlen(echoBuf));
  } else if (strcmp(path, FIRELOG_PATH) == 0) {
    char *str = "Actual log is " FIRELOG_FILE "\n";
    sizeOut = firefuse_readBuffer(buf, str, size, offset, strlen(str));
  } else if (strcmp(path, FIRESTEP_PATH) == 0) {
    const char *json = firestep_json();
    sizeOut = firefuse_readBuffer(buf, json, size, offset, strlen(json));
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
  if (is_cv_path(path)) {
    return cve_write(path, buf, bufsize, offset, fi);
  } else if (strcmp(path, ECHO_PATH) == 0) {
    if (bufsize > MAX_ECHO) {
      sprintf(echoBuf, "firefuse_write %s -> string too long (%ld > %d bytes)", path, bufsize, MAX_ECHO);
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
  if (is_cv_path(path)) {
    return cve_truncate(path, size);
  }

  (void) size;
  if (strcmp(path, "/") == 0) {
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

int main(int argc, char *argv[]) {
  fuse_root = argv[argc-1];
  return fuse_main(argc, argv, &firefuse_oper, NULL);
}


