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

// FireREST JSON network response sizes are obtained from FUSE file size.
// We provide a minimum size for sync requests that don't know actual response size in cve_getattr()
// Large values waste bandwidth, small values run the risk of clipping the returned JSON.
// To match the minimum size constraint, JSON text is padded with trailing spaces
#define MIN_SAVE_SIZE ((size_t) 256)
#define MIN_PROCESS_SIZE ((size_t) 2048)

#ifndef bool
#define bool int
#define TRUE 1
#define FALSE 0
#endif

typedef struct {
  char *pData;
  int length;
  long reserved;
} FuseDataBuffer;

extern const char * fuse_root;
extern FuseDataBuffer headcam_image;     // perpetually changing image
extern FuseDataBuffer headcam_image_fstat;  // image at time of most recent fstat()
extern int cameraWidth; // config.json provided camera width
extern int cameraHeight; // config.json provided camera height

extern FuseDataBuffer* firefuse_allocImage(const char *path, int *pResult);
extern FuseDataBuffer* firefuse_allocDataBuffer(const char *path, int *pResult, const char *pData, size_t length);
bool firefuse_isFile(const char *value, const char * suffix);
extern void firefuse_freeDataBuffer(const char *path, struct fuse_file_info *fi);
extern const char* firepick_status();
extern const void* firepick_holes(FuseDataBuffer *pJPG);
extern int background_worker();
extern bool is_cv_path(const char * path);
extern bool is_cnc_path(const char *path);
int firefuse_getattr_file(const char *path, struct stat *stbuf, size_t length, int perm);
int firefuse_getattr(const char *path, struct stat *stbuf);
int firefuse_open(const char *path, struct fuse_file_info *fi);
int firefuse_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int firefuse_write(const char *path, const char *buf, size_t bufsize, off_t offset, struct fuse_file_info *fi);
int firefuse_release(const char *path, struct fuse_file_info *fi);
int firefuse_main(int argc, char *argv[]);

int firerest_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi);
int firerest_getattr_default(const char *path, struct stat *stbuf);
char * firerest_config(const char *path);

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

#define FIREREST_1 "/1"
#define FIREREST_GRAY "/gray"
#define FIREREST_BGR "/bgr"
#define FIREREST_CAMERA_JPG "/camera.jpg"
#define FIREREST_CV "/cv"
#define FIREREST_SYNC "/sync"
#define FIREREST_CVE "/cve"
#define FIREREST_CNC "/cnc"
#define FIREREST_FIRESIGHT_JSON "/firesight.json"
#define FIREREST_PROPERTIES_JSON "/properties.json"
#define FIREREST_MONITOR_JPG "/monitor.jpg"
#define FIREREST_OUTPUT_JPG "/output.jpg"
#define FIREREST_PROCESS_FIRE "/process.fire"
#define FIREREST_GCODE_FIRE "/gcode.fire"
#define FIREREST_SAVED_PNG "/saved.png"
#define FIREREST_SAVE_FIRE "/save.fire"

#define FIREREST_VAR "/var/firefuse"

bool cve_isPathSuffix(const char *path, const char *suffix);
int cve_save(FuseDataBuffer *pBuffer, const char *path);
int cve_getattr(const char *path, struct stat *stbuf);
int cve_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi);
int cve_open(const char *path, struct fuse_file_info *fi);
int cve_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int cve_write(const char *path, const char *buf, size_t bufsize, off_t offset, struct fuse_file_info *fi);
int cve_release(const char *path, struct fuse_file_info *fi);
int cve_truncate(const char *path, off_t size);
int cnc_getattr(const char *path, struct stat *stbuf);
int cnc_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi);
int cnc_open(const char *path, struct fuse_file_info *fi);
int cnc_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int cnc_write(const char *path, const char *buf, size_t bufsize, off_t offset, struct fuse_file_info *fi);
int cnc_release(const char *path, struct fuse_file_info *fi);
int cnc_truncate(const char *path, off_t size);

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
    LOGERROR2("verifyOpenRW(%s) DIRECTORY EACCESS %o", path, fi->flags);
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
	LOGERROR2("verifyOpenRW(%s) EACCESS %o", path, fi->flags);
	(*pResult) = -EACCES;
	break;
      default:
	LOGERROR2("verifyOpenRW(%s) UNKNOWN EACCESS %o", path, fi->flags);
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
#include <vector>
#include <map>
#include <string.h>

double 	cve_seconds();
void 	cve_process(const char *path, int *pResult);
class BackgroundWorker;

typedef class CVE {
  private: string name;
  private: bool _isColor;

  public: LIFOCache<SmartPointer<char> > src_saved_png;
  public: LIFOCache<SmartPointer<char> > src_save_fire;
  public: LIFOCache<SmartPointer<char> > src_process_fire;
  public: LIFOCache<SmartPointer<char> > src_firesight_json;
  public: LIFOCache<SmartPointer<char> > src_properties_json;
  public: static string cve_path(const char *pPath);
  public: CVE(string name);
  public: ~CVE();
  public: inline string getName() { return name; }
  public: int save(BackgroundWorker *pWorker);
  public: int process(BackgroundWorker *pWorker);
  public: inline bool isColor() { return _isColor; }
} CVE, *CVEPtr;

typedef class DCE {
  private: string name;
  private: int serial_fd;
  private: string serial_path;
  private: string serial_stty;
  private: int sendSerial(const char *text);

  protected: virtual void send(SmartPointer<char> request, json_t*response);

  // Common data
  public: LIFOCache<SmartPointer<char> > snk_gcode_fire;
  public: LIFOCache<SmartPointer<char> > src_gcode_fire;
  //public: LIFOCache<SmartPointer<char> > src_properties_json;
  
  public: static int callSystem(char *cmdbuf);
  public: static string dce_path(const char *pPath);
  public: DCE(string name);
  public: ~DCE();
  public: void clear();
  public: int serial_init();
  public: inline string getName() { return name; }
  public: int gcode(BackgroundWorker *pWorker);
  public: inline string getSerialStty() { return serial_stty; }
  public: inline void setSerialStty(const char * value) { serial_stty = value; }
  public: inline string getSerialPath() { return serial_path; }
  public: inline void setSerialPath(const char * value) { serial_path = value; }
} DCE, *DCEPtr;

typedef class CameraNode {
  private: double output_seconds; // time of last FireSight pipeline completion
  private: double monitor_duration; // number of seconds to show last output

  // Common data
  public: LIFOCache<SmartPointer<char> > src_camera_jpg;
  public: LIFOCache<Mat> src_camera_mat_gray;
  public: LIFOCache<Mat> src_camera_mat_bgr;
  public: LIFOCache<SmartPointer<char> > src_monitor_jpg;
  public: LIFOCache<SmartPointer<char> > src_output_jpg;

  // For BackgroundWorker use
  public: CameraNode();
  public: ~CameraNode();
  public: void init();
  public: int async_update_camera_jpg();
  public: int async_update_monitor_jpg();
  public: void setOutput(Mat image);

  public: void temp_set_output_seconds() { output_seconds = cve_seconds(); }
} CameraNode;

typedef class BackgroundWorker {
  private: double idle_period; // minimum seconds between idle() execution
  private: std::map<string, CVEPtr> cveMap;
  private: std::map<string, DCEPtr> dceMap;;
  private: double idle_seconds; // time of last idle() execution
  private: int async_save_fire();
  private: int async_process_fire();
  private: int async_gcode_fire();

  public: CameraNode cameras[1];

  public: BackgroundWorker();
  public: ~BackgroundWorker();
  public: CVE& cve(string path, bool create=FALSE);
  public: DCE& dce(string path, bool create=FALSE);
  public: vector<string> getCveNames();
  public: vector<string> getDceNames();
  public: void clear();
  public: void process();
  public: inline void setIdlePeriod(double value) { idle_period = value; }
  public: inline double getIdlePeriod() { return idle_period; }

  // TESTING ONLY
  public: void processInit();
  public: int processLoop();
  public: void idle();
} BackgroundWorker;

extern BackgroundWorker worker; // BackgroundWorker singleton background worker

typedef class JSONFileSystem {
  private: std::map<string, json_t *> dirMap;
  private: std::map<string, json_t *> fileMap;
  private: json_t *resolve_file(const char *path);

  public: JSONFileSystem();
  public: ~JSONFileSystem();

  public: static vector<string> splitPath(const char *path);
  public: void clear();
  public: json_t *get(const char *path);
  public: void create_file(const char *path, int perm);
  public: inline void create_file(string path, int perm) { create_file(path.c_str(), perm); }
  public: vector<string> fileNames(const char *path);
  public: bool isFile(const char *path);
  public: bool isDirectory(const char *path);
  public: int perms(const char *path);
} JSONFileSystem;

typedef class FireREST {
  private: pthread_mutex_t processMutex;
  private: int processCount;
  private: JSONFileSystem files;
  private: string config_camera(const char* cv_path, json_t *pCamera, const char *pCameraName, json_t *pCveMap);
  private: string config_cv(const char* root_path, json_t *pConfig);
  private: string config_cnc(const char* root_path, json_t *pConfig);
  private: string config_cnc_serial(string dcePath, json_t *pSerial);
  private: void create_resource(string path, int perm);

  public: FireREST();
  public: ~FireREST();

  public: int incrementProcessCount();
  public: int decrementProcessCount();
  public: char * configure_path(const char *path);
  public: void configure_json(const char *pJson);
  public: int perms(const char *path) { return files.perms(path); }
  public: bool isDirectory(const char *path) { return files.isDirectory(path); }
  public: bool isFile(const char *path) { return files.isFile(path); }
  public: static bool isSync(const char *path);
  public: vector<string> fileNames(const char *path) { return files.fileNames(path); }
} FireREST;

extern FireREST firerest;

#endif
//////////////////////////////////// FIREFUSE_H ////////////////////////////////////////////////////////
#endif
