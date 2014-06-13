#ifndef FIREFUSE_HPP
#define FIREFUSE_HPP
#ifdef __cplusplus
extern "C" {
#endif

//////////////////////////////////// C DECLARATIONS ////////////////////////////////////////////////////////
#define FUSE_USE_VERSION 26
#include <fuse.h>
#include <FireLog.h>

#define MAX_GCODE_LEN 255 /* maximum characters in a gcode instruction */

#define STATUS_PATH "/status"
#define HOLES_PATH "/holes"
#define FIRESTEP_PATH "/firestep"
#define FIRELOG_PATH "/firelog"
#define CONFIG_PATH "/config.json"
#define ECHO_PATH "/echo"

typedef struct {
  char *pData;
  int length;
  long reserved;
} FuseDataBuffer;

extern const char * fuse_root;
extern FuseDataBuffer headcam_image;     // perpetually changing image
extern FuseDataBuffer headcam_image_fstat;  // image at time of most recent fstat()

extern FuseDataBuffer* firefuse_allocImage(const char *path, int *pResult);
extern FuseDataBuffer* firefuse_allocDataBuffer(const char *path, int *pResult, const char *pData, size_t length);
extern void firefuse_freeDataBuffer(const char *path, struct fuse_file_info *fi);
extern const char* firepick_status();
extern const void* firepick_holes(FuseDataBuffer *pJPG);
extern int background_worker();

static inline int firefuse_readBuffer(char *pDst, const char *pSrc, size_t size, off_t offset, size_t len) {
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

#ifndef bool
#define bool int
#define TRUE 1
#define FALSE 0
#endif

enum CVE_Path {
    CVEPATH_CAMERA_SAVE=1,
    CVEPATH_CAMERA_LOAD=2,
    CVEPATH_PROCESS_JSON=4
};

#define FIREREST_1 "/1"
#define FIREREST_GRAY "/gray"
#define FIREREST_BGR "/bgr"
#define FIREREST_CAMERA_JPG "/camera.jpg"
#define FIREREST_CV "/cv"
#define FIREREST_CVE "/cve"
#define FIREREST_FIRESIGHT_JSON "/firesight.json"
#define FIREREST_PROPERTIES_JSON "/properties.json"
#define FIREREST_MONITOR_JPG "/monitor.jpg"
#define FIREREST_OUTPUT_JPG "/output.jpg"
#define FIREREST_PROCESS_FIRE "/process.fire"
#define FIREREST_SAVED_PNG "/saved.png"
#define FIREREST_SAVE_FIRE "/save.fire"

#define FIREREST_VAR "/var/firefuse"

bool cve_isPathPrefix(const char *value, const char * prefix) ;
bool cve_isPathSuffix(const char *path, const char *suffix);
int cve_save(FuseDataBuffer *pBuffer, const char *path);
int cve_getattr(const char *path, struct stat *stbuf);
int cve_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi);
int cve_open(const char *path, struct fuse_file_info *fi);
int cve_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int cve_write(const char *path, const char *buf, size_t bufsize, off_t offset, struct fuse_file_info *fi);
int cve_release(const char *path, struct fuse_file_info *fi);
int cve_truncate(const char *path, off_t size);

void firerest_config(const char *pJson);

inline bool verifyOpenR_(const char *path, struct fuse_file_info *fi, int *pResult) {
  switch (fi->flags & 3) {
    case O_RDONLY:
      LOGTRACE1("verifyOpenR_(%s) O_RDONLY", path);
      break;
    case O_WRONLY:
    case O_RDWR:
    default:
      LOGERROR2("verifyOpenR_(%s) EACCESS %o", path, fi->flags);
      (*pResult) = -EACCES;
      break;
  }
  return (*pResult) == 0;
}

inline bool verifyOpenRW(const char *path, struct fuse_file_info *fi, int *pResult) {
  if ((fi->flags & O_DIRECTORY)) {
    LOGERROR2("verifyOpenRW(%s) EACCESS %o", path, fi->flags);
    (*pResult) = -EACCES;
  } else {
    switch (fi->flags & 3) {
      case O_RDONLY:
	LOGTRACE1("verifyOpenRW(%s) O_RDONLY", path);
	break;
      case O_WRONLY:
	LOGTRACE1("verifyOpenRW(%s) O_WRONLY", path);
	break;
      case O_RDWR: // Simultaneous read/write not allowed
      default:
	LOGERROR2("verifyOpenRW(%s) EACCESS %o", path, fi->flags);
	(*pResult) = -EACCES;
	break;
    }
  }
  return (*pResult) == 0;
}

int firestep_init();
void firestep_destroy();
int firestep_write(const char *buf, size_t bufsize);
const char * firestep_json();

#ifdef __cplusplus
//////////////////////////////////// C++ DECLARATIONS ////////////////////////////////////////////////////////
} // __cplusplus
#include "LIFOCache.hpp"

double 	cve_seconds();
void 	cve_process(const char *path, int *pResult);
string 	cve_path(const char *pPath);
class DataFactory;

typedef class CVE {
  private: string name;
  private: bool _isColor;
  public: LIFOCache<SmartPointer<char> > src_saved_png;
  public: LIFOCache<SmartPointer<char> > src_save_fire;
  public: LIFOCache<SmartPointer<char> > src_process_fire;
  public: LIFOCache<SmartPointer<char> > src_firesight_json;
  public: LIFOCache<SmartPointer<char> > src_properties_json;
  public: inline string getName() { return name; }
  public: CVE(string name);
  public: ~CVE();
  public: int save(DataFactory *pFactory);
  public: int process(DataFactory *pFactory);
  public: inline bool isColor() { return _isColor; }
} CVE, *CVEPtr;

class CameraNode {
  private: double output_seconds; // time of last FireSight pipeline completion
  private: double monitor_duration; // number of seconds to show last output

  // Common data
  public: LIFOCache<SmartPointer<char> > src_camera_jpg;
  public: LIFOCache<Mat> src_camera_mat_gray;
  public: LIFOCache<Mat> src_camera_mat_bgr;
  public: LIFOCache<SmartPointer<char> > src_monitor_jpg;
  public: LIFOCache<SmartPointer<char> > src_output_jpg;

  // For DataFactory use
  public: CameraNode();
  public: ~CameraNode();
  public: void init();
  public: int async_update_camera_jpg();
  public: int async_update_monitor_jpg();
  public: void setOutput(Mat image);

  public: void temp_set_output_seconds() { output_seconds = cve_seconds(); }
};

class DataFactory {
  private: double idle_period; // minimum seconds between idle() execution
  private: std::map<string, CVEPtr> cveMap;
  private: double idle_seconds; // time of last idle() execution
  private: int async_save_fire();
  private: int async_process_fire();

  public: CameraNode cameras[1];

  public: DataFactory();
  public: ~DataFactory();
  public: CVE& cve(string path);
  public: vector<string> getCveNames();
  public: void clear();
  public: void process();
  public: inline void setIdlePeriod(double value) { idle_period = value; }
  public: inline double getIdlePeriod() { return idle_period; }

  // TESTING ONLY
  public: void processInit();
  public: int processLoop();
  public: void idle();
};

extern DataFactory factory; // DataFactory singleton background worker

#endif
//////////////////////////////////// FIREFUSE_H ////////////////////////////////////////////////////////
#endif
