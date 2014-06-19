#include "FireSight.hpp"
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>
#include <time.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include "firefuse.h"
#include "version.h"
#include "FirePiCam.h"

template <class T> class MockValue {
private:
  T value;

public:
  MockValue(T aValue) {
    cout << "MockValue(" << aValue << ")" << " " << (long) this << endl;
    value = aValue;
  }

  MockValue() {
    cout << "MockValue(0)" << " " << (long) this << endl;
    value = 0;
  }

  MockValue(const MockValue &that) {
    cout << "copy MockValue(" << that.value << ")" << " " << (long) this << endl;
    value = that.value;
  }

  ~MockValue() {
    cout << "~MockValue(" << value << ")" << " " << (long) this << endl;
  }

  MockValue& operator=( const MockValue& that ) {
    cout << "MockValue::operator=(" << that.value << ")" << " " << (long) this << endl;
    value = that.value;
    return *this;
  }

  T getValue() {
    cout << "MockValue.getValue() => " << value << " " << (long) this << endl;
    return value;
  }
  void setValue(T value) {
    this->value = value;
  }
};

template <class T> bool testNumber(T expected, T actual) {
  if (expected != actual) {
    LOGERROR2("expected:%ld actual: %ld", expected, actual);
    return false;
  }
  return true;
}

bool testString(const char * name, const char*expected, const char*actualValue) {
  if (strcmp(expected, actualValue)) {
    LOGTRACE3("TEST %s expected:\"%s\" actual:\"%s\"", name, expected, actualValue);
    return false;
  }
  LOGTRACE2("TEST %s ok:\"%s\"", name, actualValue);
  return true;
}

bool testString(const char * name, const char*expected, SmartPointer<char> actual) {
  string actualValue(actual.data(), actual.data() + actual.size());
  if (strlen(expected) != actualValue.size() || strncmp(expected, actualValue.c_str(), strlen(expected))) {
    LOGTRACE3("TEST %s expected:\"%s\" actual:\"%s\"", name, expected, actualValue.c_str());
    return false;
  }
  return true;
}

bool testFile(const char * title, const char * path, SmartPointer<char> &contents, const char *pWriteData = NULL) {
  int perm = pWriteData ? 0666 : 0444;
  struct fuse_file_info file_info;
  struct stat file_stat;
  int rc;

  rc = cve_getattr(path, &file_stat);
  LOGINFO4("TEST %s st_size:%ld perm:%o contents.size():%ld", title, file_stat.st_size, file_stat.st_mode & 0777, contents.size());
  assert(rc == 0);
  assert(file_stat.st_uid == getuid());
  assert(file_stat.st_gid == getgid());
  assert(file_stat.st_atime == file_stat.st_mtime);
  assert(file_stat.st_atime == file_stat.st_ctime);
  assert(file_stat.st_atime);
  assert(file_stat.st_nlink == 1);
  assert(file_stat.st_mode == (S_IFREG | perm));
  assert(file_stat.st_size == contents.size());
  memset(&file_info, 0, sizeof(fuse_file_info));
  file_info.flags = O_RDONLY;
  rc = cve_open(path, &file_info);
  assert(rc == 0);

  if (pWriteData) {
    char buf[101];
    // TEST WRITE
    size_t len = strlen(pWriteData);
    assert(len < 100);	// test limitation
    rc = cve_write(path, pWriteData, len, 0, &file_info);
    assert(rc == len);
    rc = cve_release(path, &file_info);
    assert(rc == 0);

    // VERIFY WRITE
    file_info.flags = O_WRONLY;
    rc = cve_open(path, &file_info);
    assert(rc == 0);
    memset(buf, 0, 101);
    rc = cve_getattr(path, &file_stat);
    assert(rc == 0);
    assert(file_stat.st_size == len);
    rc = cve_read(path, buf, len, 0, &file_info);
    assert(rc == len);
    assert(0 == strcmp(buf, pWriteData));
  }

  rc = cve_release(path, &file_info);
  assert(rc == 0);


  return true;
}

int testSmartPointer( ){
  cout << "testSmartPointer() ------------------------" << endl;
  SmartPointer<char> empty(NULL, 0);
  assert(0 == empty.size());
  assert(!empty.data());
  char *pOne = (char*)malloc(100);
  strcpy(pOne, "one");
  cout << "pOne == " << (long) pOne << endl;
  char *pTwo = (char*)malloc(100);
  strcpy(pTwo, "two");
  cout << "pTwo == " << (long) pTwo << endl;
  MockValue<int> *pMock = new MockValue<int>(123);
  cout << "pMock == " << (long) pMock << endl;
  {
    cout << "entering nested block" << endl;
    SmartPointer<char> zero;
    assert(NULL == (char*) zero);
    assert(0 == zero.size());
    SmartPointer<char> one(pOne);
    assert(0 == one.size());
    assert(pOne == (char*)one);
    assert(0 == strcmp("one", (char*)one));
    assert(1 == one.getReferences());
    assert(0 == strcmp("one", (char*)one));
    assert(0 == strcmp("one", pOne));
    void *hideOne = &one;
    LOGTRACE("TEST one = one START");
    one = *(SmartPointer<char> *) hideOne;
    LOGTRACE("TEST one = one END");
    assert(0 == strcmp("one", (char*)one));
    assert(1 == one.getReferences());
    assert(0 == strcmp("one", (char*)one));
    assert(0 == strcmp("one", pOne));
    SmartPointer<char> oneCopy(one);
    assert(0 == strcmp("one", (char*)oneCopy));
    assert(one.size() == oneCopy.size());
    assert(2 == one.getReferences());
    assert(2 == oneCopy.getReferences());
    SmartPointer<char> two(pTwo);
    assert(0 == strcmp("two", (char*)two));
    assert(0 != strcmp((char *) one, (char *) two));
    one = two;
    assert(one.size() == two.size());
    assert(0 == strcmp((char *) one, (char *) two));
    assert(2 == one.getReferences());
    assert(1 == oneCopy.getReferences());

    SmartPointer<MockValue<int> > mock(pMock);
    assert(123 == mock->getValue());
    cout << "leaving nested block" << endl;
  }
  assert('o' != *pOne); // dereference freed memory
  assert('t' != *pTwo); // dereference freed memory
  cout << "testSmartPointer() PASSED" << endl;
  cout << endl;

  return 0;
}

int testSmartPointer_CopyData( ){
  cout << "testSmartPointer_CopyData() ------------------------" << endl;
  char *pOne = (char*)malloc(100);
  strcpy(pOne, "one");
  cout << "pOne == " << (long) pOne << endl;
  char *pTwo = (char*)malloc(200);
  strcpy(pTwo, "two");
  cout << "pTwo == " << (long) pTwo << endl;
  MockValue<int> *pMock = new MockValue<int>(123);
  cout << "pMock == " << (long) pMock << endl;
  {
    cout << "entering nested block" << endl;
    SmartPointer<char> zero;
    assert(NULL == (char*) zero);
    assert(0 == zero.size());
    SmartPointer<char> one(pOne, 100);		// COPY DATA ONLY
    assert(100 == one.size());
    assert(pOne != (char*)one);			// COPY DATA ONLY
    strcpy(pOne, "DEAD");			// COPY DATA ONLY
    assert(0 == strcmp("one", (char*) one));
    free(pOne);					// COPY DATA ONLY
    assert(1 == one.getReferences());
    assert(0 == strcmp("one", one.data()));
    assert(0 != strcmp("one", pOne));		// COPY DATA ONLY
    SmartPointer<char> oneCopy(one);
    assert(0 == strcmp("one", (char*)oneCopy));
    assert(one.size() == oneCopy.size());
    assert(2 == one.getReferences());
    assert(2 == oneCopy.getReferences());
    SmartPointer<char> two(pTwo);
    assert(0 == strcmp("two", (char*)two));
    assert(0 != strcmp((char *) one, (char *) two));
    one = two;
    assert(one.size() == two.size());
    assert(0 == strcmp((char *) one, (char *) two));
    assert(2 == one.getReferences());
    assert(1 == oneCopy.getReferences());

    SmartPointer<MockValue<int> > mock(pMock, 1); // COPY DATA ONLY
    pMock->setValue(456);			  // COPY DATA ONLY
    free(pMock);				  // COPY DATA ONLY
    assert(123 == mock->getValue());
    cout << "leaving nested block" << endl;
  }
  assert('o' != *pOne); // dereference freed memory
  assert('t' != *pTwo); // dereference freed memory
  cout << "testSmartPointer_CopyData() PASSED" << endl;
  cout << endl;

  return 0;
}

typedef SmartPointer<char> CharPtr;

LIFOCache<int> bgCache;

static void * lifo_thread(void *arg) {
  LOGTRACE("lifo_thread() START");
  while (bgCache.isFresh()) { sched_yield(); }
  LOGTRACE("lifo_thread() post 100");
  bgCache.post(100);

  while (bgCache.isFresh()) { sched_yield(); }
  LOGTRACE("lifo_thread() post 200");
  bgCache.post(200);

  while (bgCache.isFresh()) { sched_yield(); }
  LOGTRACE("lifo_thread() post 300");
  bgCache.post(300);

  while (bgCache.isFresh()) { sched_yield(); }
  LOGTRACE("lifo_thread() post 400");
  bgCache.post(400);
 
  while (bgCache.isFresh()) { sched_yield(); }
  LOGTRACE("lifo_thread() post 500");
  bgCache.post(500);
 
  return NULL;
}

int testLIFOCache() {
  cout << "testLIFOCache() ------------------------" << endl;
  {
    LIFOCache<MockValue<int> > bufInt;
    assert(!bufInt.isFresh());
    bufInt.post(MockValue<int>(1));
    assert(bufInt.isFresh());

    cout << "testLIFOCache() get" << endl;
    assert(bufInt.isFresh());
    assert(1 == bufInt.peek().getValue());
    assert(bufInt.isFresh());
    assert(1 == bufInt.get().getValue());
    assert(!bufInt.isFresh());
    assert(1 == bufInt.get().getValue());
    assert(!bufInt.isFresh());
    assert(1 == bufInt.peek().getValue());
    assert(!bufInt.isFresh());
   
    cout << "testLIFOCache() post" << endl;
    bufInt.post(MockValue<int>(2));
    assert(2 == bufInt.get().getValue());
    bufInt.post(MockValue<int>(3));
    bufInt.post(MockValue<int>(4));
    cout << "Overwritten value: " << bufInt.peek().getValue() << endl;
    assert(4 == bufInt.get().getValue());

    LIFOCache<string> bufString;
    assert(!bufString.isFresh());
    bufString.post("one");
    assert(bufString.isFresh());
    assert(0 == strcmp("one", bufString.peek().c_str()));
    assert(bufString.isFresh());
    assert(0 == strcmp("one", bufString.get().c_str()));
    assert(!bufString.isFresh());
    assert(0 == strcmp("one", bufString.peek().c_str()));
    assert(!bufString.isFresh());
    assert(0 == strcmp("one", bufString.get().c_str()));
    assert(!bufString.isFresh());

    bufString.post("two");
    assert(bufString.isFresh());
    assert(0 == strcmp("two", bufString.peek().c_str()));
    assert(bufString.isFresh());
    assert(0 == strcmp("two", bufString.get().c_str()));
    assert(!bufString.isFresh());
    assert(0 == strcmp("two", bufString.get().c_str()));

    char *one = (char *) malloc(123456);
    strcpy(one, "one");
    cout << "one " << (long) one << endl;
    char *two = (char *) malloc(123456);
    cout << "two " << (long) two << endl;
    strcpy(two, "two");
    {
      CharPtr spOne(one);
      assert(1 == spOne.getReferences());
      CharPtr spTwo(two);
      LIFOCache<CharPtr> bufCharPtr;
      assert(!bufCharPtr.isFresh());
      assert(NULL == (char *)bufCharPtr.peek());
      assert(!bufCharPtr.isFresh());
      assert(NULL == (char *)bufCharPtr.get());
      assert(!bufCharPtr.isFresh());
      assert(0 == bufCharPtr.peek().getReferences());
      assert(0 == bufCharPtr.get().getReferences());
      bufCharPtr.post(spOne);
      assert(bufCharPtr.isFresh());
      assert(one == (char *)bufCharPtr.peek());
      assert(bufCharPtr.isFresh());
      assert(one == (char *)bufCharPtr.get());
      assert(!bufCharPtr.isFresh());
      assert(3 == spOne.getReferences()); 
      assert(1 == spTwo.getReferences()); 
      bufCharPtr.post(spTwo);
      assert(bufCharPtr.isFresh());
      assert(2 == spOne.getReferences());
      assert(2 == spTwo.getReferences());
      bufCharPtr.peek();
      assert(2 == spOne.getReferences());
      assert(2 == spTwo.getReferences());
      bufCharPtr.get();
      assert(1 == spOne.getReferences());
      assert(3 == spTwo.getReferences());
      bufCharPtr.post(spTwo);
      assert(1 == spOne.getReferences());
      assert(3 == spTwo.getReferences());
      assert(bufCharPtr.isFresh());
      assert(two == (char *)bufCharPtr.peek());
      assert(bufCharPtr.isFresh());
      assert(two == (char *)bufCharPtr.get());
      assert(!bufCharPtr.isFresh());
      assert(1 == spOne.getReferences());
      assert(3 == spTwo.getReferences());
    }
    assert('o' != *one);
    assert('t' != *two);
  }

  LOGTRACE("TEST phtread_create()");
  pthread_t tidLIFO;
  int rc = pthread_create(&tidLIFO, NULL, &lifo_thread, NULL);
  int firstValue = bgCache.get_sync();
  LOGTRACE1("TEST get_sync() => %d", firstValue);
  assert(firstValue == 100);

  int secondValue = bgCache.get_sync(100);
  LOGTRACE1("TEST get_sync(100) => %d", secondValue);
  assert(secondValue > firstValue);

  int bgValue;
  do {
    int oldValue = bgValue;
    bgValue = bgCache.get();
    assert(bgValue >= secondValue);
    if (bgValue != oldValue) {
      LOGTRACE1("TEST get() => %d", bgValue);
    }
  } while (bgValue < 500);

  cout << endl;
  cout << "testLIFOCache() PASS" << endl;
  cout << endl;

  return 0;
}

static void assert_headcam(SmartPointer<char> jpg, int headcam) {
  char message[100];
  snprintf(message, sizeof(message), "jpg.data()[1520] %0x", jpg.data()[1520]);
  cout << message << endl;
  switch (headcam) {
    case 0:
      assert(129579 == jpg.size());
      assert(0xdc == (uchar)jpg.data()[1520]);
      break;
    case 1:
      assert(128948 == jpg.size());
      assert(0x32 == (uchar)jpg.data()[1520]);
      break;
    default:
      assert(FALSE);
      break;
  }
}

bool testProcess(int expectedProcessed) {
  int actualProcessed = worker.processLoop();
  LOGTRACE2("TEST worker.processLoop() expected:%o actual:%o", expectedProcessed, actualProcessed);
  return expectedProcessed == actualProcessed;
}

int testCamera() {
  cout << "testCamera() --------------------------" << endl;
  SmartPointer<char> jpg;
  worker.processInit();
  assert(!worker.cameras[0].src_camera_jpg.isFresh());
  assert(!worker.cameras[0].src_camera_mat_gray.isFresh());
  assert(!worker.cameras[0].src_camera_mat_bgr.isFresh());
  assert(!worker.cameras[0].src_monitor_jpg.isFresh());
  assert(!worker.cameras[0].src_output_jpg.isFresh());

  assert(testProcess(0200007));
  assert(!worker.cameras[0].src_camera_jpg.isFresh());
  assert(worker.cameras[0].src_camera_mat_gray.isFresh());
  assert(worker.cameras[0].src_camera_mat_bgr.isFresh());
  assert(worker.cameras[0].src_monitor_jpg.isFresh());
  assert(!worker.cameras[0].src_output_jpg.isFresh());
  jpg = worker.cameras[0].src_camera_jpg.peek();
  assert_headcam(jpg, 0);

  assert(testProcess(07));
  assert(worker.cameras[0].src_camera_jpg.isFresh());
  assert(worker.cameras[0].src_camera_mat_gray.isFresh());
  assert(worker.cameras[0].src_camera_mat_bgr.isFresh());
  assert(worker.cameras[0].src_monitor_jpg.isFresh());
  assert(!worker.cameras[0].src_output_jpg.isFresh());
  jpg = worker.cameras[0].src_camera_jpg.peek();
  assert_headcam(jpg, 1);

  assert(testProcess(0));
  assert(worker.cameras[0].src_camera_jpg.isFresh());
  assert(worker.cameras[0].src_camera_mat_gray.isFresh());
  assert(worker.cameras[0].src_camera_mat_bgr.isFresh());
  assert(worker.cameras[0].src_monitor_jpg.isFresh());
  assert(!worker.cameras[0].src_output_jpg.isFresh());
  jpg = worker.cameras[0].src_camera_jpg.peek();
  assert_headcam(jpg, 1);

  worker.setIdlePeriod(0.1d);
  cout << "idle: " << worker.getIdlePeriod() << endl;
  assert(0.1d == worker.getIdlePeriod());
  usleep(100000);
  assert(testProcess(0));
  assert(worker.cameras[0].src_camera_jpg.isFresh());
  assert(worker.cameras[0].src_camera_mat_gray.isFresh());
  assert(worker.cameras[0].src_camera_mat_bgr.isFresh());
  assert(!worker.cameras[0].src_monitor_jpg.isFresh());  // consumed by idle()
  assert(!worker.cameras[0].src_output_jpg.isFresh());
  jpg = worker.cameras[0].src_camera_jpg.peek();
  assert_headcam(jpg, 1);
  worker.setIdlePeriod(0);

  assert(testProcess(0200000));
  assert(!worker.cameras[0].src_camera_jpg.isFresh());
  assert(worker.cameras[0].src_camera_mat_gray.isFresh());
  assert(worker.cameras[0].src_camera_mat_bgr.isFresh());
  assert(worker.cameras[0].src_monitor_jpg.isFresh());
  assert(!worker.cameras[0].src_output_jpg.isFresh());
  jpg = worker.cameras[0].src_camera_jpg.peek();
  assert_headcam(jpg, 1);

  assert(testProcess(07));
  assert(worker.cameras[0].src_camera_jpg.isFresh());
  assert(worker.cameras[0].src_camera_mat_gray.isFresh()); 
  assert(worker.cameras[0].src_camera_mat_bgr.isFresh()); 
  assert(worker.cameras[0].src_monitor_jpg.isFresh());
  assert(!worker.cameras[0].src_output_jpg.isFresh());
  jpg = worker.cameras[0].src_camera_jpg.peek();
  assert_headcam(jpg, 0);

  Mat grayImage = worker.cameras[0].src_camera_mat_gray.get();
  cout << "grayImage: " << grayImage.rows << "x" << grayImage.cols << endl;
  assert(200 == grayImage.rows);
  assert(800 == grayImage.cols);
  assert(testProcess(05));
  assert(worker.cameras[0].src_camera_jpg.isFresh());
  assert(worker.cameras[0].src_camera_mat_gray.isFresh()); 
  assert(worker.cameras[0].src_camera_mat_bgr.isFresh()); // fresh but older than camera_jpg
  assert(worker.cameras[0].src_monitor_jpg.isFresh());
  assert(!worker.cameras[0].src_output_jpg.isFresh());
  jpg = worker.cameras[0].src_camera_jpg.peek();
  assert_headcam(jpg, 1);

  cout << "testCamera() PASS" << endl;
  cout << endl;
  return 0;
}

int testConfig() {
  cout << "testConfig() --------------------------" << endl;
  worker.clear();
  worker.processInit();
  const char *config_json = \
  "{ \"FireREST\":{\"title\":\"Raspberry Pi FireFUSE\",\"provider\":\"FireFUSE\", \"version\":{\"major\":0, \"minor\":6, \"patch\":0}},\n" \
    "\"cv\":{\n" \
      "\"cve_map\":{\n" \
	"\"one\":{ \"firesight\": [ {\"op\":\"putText\", \"text\":\"one\"} ], \"properties\": { \"caps\":\"ONE\" } },\n" \
	"\"two\":{ \"firesight\": [ {\"op\":\"putText\", \"text\":\"two\"} ], \"properties\": { \"caps\":\"TWO\" } }\n" \
      "},\n" \
      "\"camera_map\":{\n" \
	"\"1\":{ \
	  \"width\":400,\
	  \"height\":400,\
	  \"profile_map\":{ \"gray\":{ \"cve_names\":[ \"one\", \"two\" ] }, \"bgr\":{ \"cve_names\":[ \"one\", \"two\" ] }}}\n" \
      "}\n" \
    "}\n" \
  "}\n"; 

  assert(testString("TEST fuse_root", "/dev/firefuse", fuse_root));

  /////////// config test
  firerest_config(config_json);
  vector<string> cveNames = worker.getCveNames();
  assert(4 == cveNames.size());
  for (int i = 0; i < 4; i++) {
    LOGINFO2("TEST config_json cveNames[%d] = %s", i, cveNames[i].c_str());
  }
  assert(0==strcmp("/cv/1/bgr/cve/one", cveNames[0].c_str()));
  assert(0==strcmp("/cv/1/bgr/cve/two", cveNames[1].c_str()));
  assert(0==strcmp("/cv/1/gray/cve/one", cveNames[2].c_str()));
  assert(0==strcmp("/cv/1/gray/cve/two", cveNames[3].c_str()));
  SmartPointer<char> one_json(worker.cve("/cv/1/gray/cve/one").src_firesight_json.get());
  assert(testString("firesight.json GET","[{\"op\":\"putText\",\"text\":\"one\"}]", one_json));
  assert(400 == cameraWidth);
  assert(400 == cameraHeight);

  //////////////// properties test
  const char * twoPath = "/cv/1/bgr/cve/two/properties.json";
  SmartPointer<char> two_properties(worker.cve(twoPath).src_properties_json.get());
  assert(testString("properties.json GET","{\"caps\":\"TWO\"}", two_properties));
  testFile("properties.json", twoPath, two_properties, "{\"caps\":\"TUTU\"}");
  string syncTwo("/sync");
  syncTwo += twoPath;
  LOGTRACE2("twoPath:%lx syncTwo:%lx", (ulong) &worker.cve(twoPath), (ulong) &worker.cve(syncTwo.c_str()));
  assert(&worker.cve(twoPath) == &worker.cve(syncTwo.c_str()));

  assert(is_cv_path("/dev/firefuse/cv"));
  assert(is_cv_path("/dev/firefuse/sync"));
  assert(is_cv_path("/cv"));
  assert(is_cv_path("/sync"));
  assert(is_cv_path("/sync/cv"));
  assert(!firerest.isFile("/"));
  assert(firerest.isDirectory("/"));
  assert(!firerest.isFile("/cv"));
  assert(firerest.isDirectory("/cv"));
  assert(!firerest.isFile("/cv/1"));
  assert(firerest.isDirectory("/cv/1"));
  assert(firerest.isFile("/cv/1/bgr/cve/one/firesight.json"));
  assert(!firerest.isDirectory("/cv/1/bgr/cve/one/firesight.json"));
  assert(firerest.isFile("/cv/1/monitor.jpg"));
  assert(!firerest.isDirectory("/cv/1/monitor.jpg"));
  assert(!firerest.isFile("/cv/1/bgr/cve/one"));
  assert(firerest.isDirectory("/cv/1/bgr/cve/one"));

  cout << "testConfig() PASS" << endl;
  cout << endl;
  return 0;
}

int testCve() {
  char buf[100];
  /////////// process.fire test
  cout << "testCve() --------------------------" << endl;
  worker.clear();
  worker.processInit();

  //////////// cve_path
  const char *expectedPath = "/cv/1/gray/cve/two";
  string sResult = cve_path("abc/cv/1/gray/cve/two/properties.json");
  LOGINFO1("TEST cve_path(abc/cv/1/gray/cve/two/properties.json) -> %s", sResult.c_str());
  assert(0 == strcmp(expectedPath, sResult.c_str()));
  sResult = cve_path(expectedPath);
  LOGINFO1("TEST cve_path(/cv/1/gray/cve/two) -> %s", sResult.c_str());
  assert(0 == strcmp(expectedPath, sResult.c_str()));
  
  //////////// cveNames
  vector<string> cveNames = worker.getCveNames();
  assert(0 == cveNames.size());
  string firesightPath = "/cv/1/gray/cve/calc-offset/firesight.json";
  CVE& cve = worker.cve(firesightPath);
  cveNames = worker.getCveNames();
  assert(1 == cveNames.size());
  cout << "cveNames[0]: " << cveNames[0] << endl;
  assert(0 == strcmp("/cv/1/gray/cve/calc-offset", cveNames[0].c_str()));
  assert(0 == cveNames[0].compare(cve.getName()));

  /////////// firesight.json
  cout << firesightPath << " => " << (char *)cve.src_firesight_json.peek().data() << endl;
  const char * firesightJson = "[{\"op\":\"putText\", \"text\":\"CVE::CVE()\"}]";
  assert(worker.cve(firesightPath).src_firesight_json.isFresh());
  /* GET */ SmartPointer<char> firesight_json(worker.cve(firesightPath).src_firesight_json.get());
  assert(0==strcmp(firesightJson, firesight_json.data()));
  assert(!worker.cve(firesightPath).src_firesight_json.isFresh());
  assert(testProcess(0));
  assert(!worker.cve(firesightPath).src_firesight_json.isFresh()); // we don't refresh firesight.json in background
  assert(0==strcmp(firesightJson, worker.cve(firesightPath).src_firesight_json.get().data()));

  /////////// save.fire test
  assert(worker.cameras[0].src_camera_mat_gray.isFresh());
  assert(worker.cameras[0].src_camera_mat_bgr.isFresh());
  assert(worker.cve(firesightPath).src_save_fire.isFresh());
  assert(!worker.cameras[0].src_output_jpg.isFresh()); 
  assert(testNumber((size_t)0, worker.cameras[0].src_output_jpg.peek().size()));  
  assert(testString("save.fire BEFORE", "{}",worker.cve(firesightPath).src_save_fire.peek()));
  assert(testNumber((size_t) 0, worker.cve(firesightPath).src_saved_png.peek().size()));
  assert(worker.cve(firesightPath).src_save_fire.isFresh());
  /*GET*/ SmartPointer<char> save_fire(worker.cve(firesightPath).src_save_fire.get());
  assert(testString("save.fire GET", "{}",save_fire));
  assert(!worker.cve(firesightPath).src_save_fire.isFresh());
  assert(testProcess(0100));
  assert(worker.cve(firesightPath).src_save_fire.isFresh());
  save_fire = worker.cve(firesightPath).src_save_fire.peek();
  size_t saveSize = worker.cve(firesightPath).src_saved_png.peek().size();
  assert(saveSize > 0);
  snprintf(buf, sizeof(buf), "{\"bytes\":%ld}", saveSize);
  assert(testString("save.fire processLoop ", buf, save_fire));
  SmartPointer<char> save_fire_contents = worker.cve(firesightPath).src_save_fire.peek();
  testFile("save.fire", "/cv/1/gray/cve/calc-offset/save.fire", save_fire_contents);
  assert(worker.cve(firesightPath).src_save_fire.isFresh()); // Verify async never triggered
  assert(worker.cameras[0].src_output_jpg.isFresh()); 
  assert(testNumber((size_t)43249, worker.cameras[0].src_output_jpg.peek().size()));  

  /////////// process.fire test
  assert(testString("process.fire BEFORE", "{}",worker.cve(firesightPath).src_process_fire.peek()));
  assert(worker.cve(firesightPath).src_process_fire.isFresh());
  assert(testNumber((size_t)43249, worker.cameras[0].src_output_jpg.peek().size()));
  /*GET*/ SmartPointer<char> process_fire(worker.cve(firesightPath).src_process_fire.get());
  assert(testNumber((size_t)43249, worker.cameras[0].src_output_jpg.peek().size()));
  assert(testString("process.fire GET", "{}",worker.cve(firesightPath).src_process_fire.peek()));
  assert(!worker.cve(firesightPath).src_process_fire.isFresh());
  /*ASYNC*/assert(testProcess(015)); // process + camera + mat_gray
  assert(testNumber((size_t) 40734, worker.cameras[0].src_output_jpg.peek().size()));
  assert(worker.cve(firesightPath).src_process_fire.isFresh());
  assert(testString("process.fire processLoop", "{\"s1\":{}}", worker.cve(firesightPath).src_process_fire.peek()));
  assert(testNumber((size_t)40734, worker.cameras[0].src_output_jpg.peek().size()));

  cout << "testCve() PASS" << endl;
  cout << endl;
  return 0;
}

int testFireREST() {
  cout << "testFireREST() --------------------" << endl;

  vector<string> segments = JSONFileSystem::splitPath("");
  assert(segments.size() == 0);

  segments = JSONFileSystem::splitPath("/");
  assert(segments.size() == 1);
  assert(testString("splitPath(/)", "/", segments[0].c_str()));

  segments = JSONFileSystem::splitPath("/one");
  assert(segments.size() == 2);
  assert(testString("splitPath(/one)", "/", segments[0].c_str()));
  assert(testString("splitPath(/one)", "one", segments[1].c_str()));
  
  segments = JSONFileSystem::splitPath("/one/two/");
  assert(segments.size() == 3);
  assert(testString("splitPath(/one/two/)", "/", segments[0].c_str()));
  assert(testString("splitPath(/one/two/)", "one", segments[1].c_str()));
  assert(testString("splitPath(/one/two/)", "two", segments[2].c_str()));
 
  segments = JSONFileSystem::splitPath("/one/two/three.json");
  assert(segments.size() == 4);
  assert(testString("splitPath(/one/two/three.json)", "/", segments[0].c_str()));
  assert(testString("splitPath(/one/two/three.json)", "one", segments[1].c_str()));
  assert(testString("splitPath(/one/two/three.json)", "two", segments[2].c_str()));
  assert(testString("splitPath(/one/two/three.json)", "three.json", segments[3].c_str()));

  JSONFileSystem jfs;
  char * json = json_dumps(jfs.get("/"), JSON_COMPACT|JSON_PRESERVE_ORDER);
  assert(testString("jfs.get(/)", "{}", json));
  free(json);

  jfs.create_file("/a/b/c", 123);
  json = json_dumps(jfs.get("/"), JSON_COMPACT|JSON_PRESERVE_ORDER);
  assert(testString("jfs.get(/)", "{\"a\":{\"b\":{\"c\":{\"perms\":123}}}}", json));
  free(json);
  assert(123 == jfs.perms("/a/b/c"));
  LOGTRACE1("jsf.perms(/a/b) %o", jfs.perms("/a/b"));
  LOGTRACE1("jsf.perms(/a/b/) %o", jfs.perms("/a/b/"));
  assert(0755 == jfs.perms("/a/b/"));
  assert(0755 == jfs.perms("/a/b"));
  assert(jfs.isDirectory("/"));
  assert(!jfs.isFile("/"));
  assert(jfs.isDirectory("/a"));
  assert(!jfs.isFile("/a"));
  assert(jfs.isDirectory("/a/b"));
  assert(!jfs.isFile("/a/b"));
  assert(jfs.isDirectory("/a/b/"));
  assert(!jfs.isFile("/a/b/"));
  assert(!jfs.isDirectory("/a/b/c"));
  assert(jfs.isFile("/a/b/c"));

  jfs.create_file("/a/b2/c", 234);
  json = json_dumps(jfs.get("/"), JSON_COMPACT|JSON_PRESERVE_ORDER);
  assert(testString("jfs.get(/)", "{\"a\":{\"b\":{\"c\":{\"perms\":123}},\"b2\":{\"c\":{\"perms\":234}}}}", json));
  free(json);

  vector<string> bnames = jfs.fileNames("/a/");
  assert(2 == bnames.size());
  if (0 == strcmp("b", bnames[0].c_str())) {
    assert(testString("filenames(/a/)", "b2", bnames[1].c_str()));
  } else {
    assert(testString("filenames(/a/)", "b2", bnames[0].c_str()));
    assert(testString("filenames(/a/)", "b", bnames[1].c_str()));
  }

  jfs.clear();
  json = json_dumps(jfs.get("/"), JSON_COMPACT|JSON_PRESERVE_ORDER);
  assert(testString("jfs.get(/)", "{}", json));
  free(json);
  assert(!jfs.isDirectory("/a/b/"));
  assert(!jfs.isFile("/a/b/"));
  assert(!jfs.isDirectory("/a/b"));
  assert(!jfs.isFile("/a/b"));
  assert(!jfs.isDirectory("/a/b/c"));
  assert(!jfs.isFile("/a/b/c"));

  FireREST fr;
  assert(fr.isSync("/sync/cv/1/camera.jpg"));
  assert(!fr.isSync("/cv/1/camera.jpg"));
  assert(!fr.isSync("/a/b/c"));

  cout << "testFireREST() PASS" << endl;
  cout << endl;

  return 0;
}

int testSuite() {
  worker.setIdlePeriod(0);
  firelog_level(FIRELOG_TRACE);
  try {
    if (
      testFireREST() == 0 &&
      testConfig()==0 &&
      testCamera()==0 &&
      testSmartPointer()==0 && 
      testSmartPointer_CopyData()==0 && 
      testLIFOCache()==0 &&
      testCve()==0 &&
      TRUE) {
      cout << "ALL TESTS PASS!!!" << endl;
      return 0;
    } else {
      cout << "*** TEST(S) FAILED ***" << endl;
    }
  } catch (const char *ex) {
    cout << "EXCEPTION: " << ex << endl;    
  } catch (string ex) {
    cout << "EXCEPTION: " << ex << endl;    
  } catch (json_error_t ex) {
    cout << "JSON EXCEPTION: " << ex.text << " line:" << ex.line << endl;    
  } catch (...) {
    cout << "UNKNOWN EXCEPTION"<< endl;
  }
  return -1;
}

int main(int argc, char *argv[]) {
  return testSuite();
}
