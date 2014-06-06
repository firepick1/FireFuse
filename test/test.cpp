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

bool testFile(const char * title, const char * path, SmartPointer<char> &contents) {
  struct fuse_file_info file_info;
  struct stat file_stat;
  int rc;

  rc = cve_getattr(path, &file_stat);
  LOGINFO3("TEST %s st_size:%ld contents.size():%ld", title, file_stat.st_size, contents.size());
  assert(rc == 0);
  assert(file_stat.st_uid == getuid());
  assert(file_stat.st_gid == getgid());
  assert(file_stat.st_atime == file_stat.st_mtime);
  assert(file_stat.st_atime == file_stat.st_ctime);
  assert(file_stat.st_atime);
  assert(file_stat.st_nlink == 1);
  assert(file_stat.st_mode == (S_IFREG | 0444));
  assert(file_stat.st_size == contents.size());
  rc = cve_open(path, &file_info);
  assert(rc == 0);
  // TODO verify contents
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
  int actualProcessed = factory.processLoop();
  LOGTRACE2("TEST factory.processLoop() expected:%d actual:%d", expectedProcessed, actualProcessed);
  return expectedProcessed == actualProcessed;
}

int testCamera() {
  cout << "testCamera() --------------------------" << endl;
  SmartPointer<char> jpg;
  factory.processInit();
  assert(!factory.cameras[0].src_camera_jpg.isFresh());
  assert(!factory.cameras[0].src_camera_mat_gray.isFresh());
  assert(!factory.cameras[0].src_camera_mat_bgr.isFresh());
  assert(!factory.cameras[0].src_monitor_jpg.isFresh());
  assert(!factory.cameras[0].src_output_jpg.isFresh());

  assert(testProcess(4));
  assert(!factory.cameras[0].src_camera_jpg.isFresh());
  assert(factory.cameras[0].src_camera_mat_gray.isFresh());
  assert(factory.cameras[0].src_camera_mat_bgr.isFresh());
  assert(factory.cameras[0].src_monitor_jpg.isFresh());
  assert(!factory.cameras[0].src_output_jpg.isFresh());
  jpg = factory.cameras[0].src_camera_jpg.peek();
  assert_headcam(jpg, 0);

  assert(testProcess(3));
  assert(factory.cameras[0].src_camera_jpg.isFresh());
  assert(factory.cameras[0].src_camera_mat_gray.isFresh());
  assert(factory.cameras[0].src_camera_mat_bgr.isFresh());
  assert(factory.cameras[0].src_monitor_jpg.isFresh());
  assert(!factory.cameras[0].src_output_jpg.isFresh());
  jpg = factory.cameras[0].src_camera_jpg.peek();
  assert_headcam(jpg, 1);

  assert(testProcess(0));
  assert(factory.cameras[0].src_camera_jpg.isFresh());
  assert(factory.cameras[0].src_camera_mat_gray.isFresh());
  assert(factory.cameras[0].src_camera_mat_bgr.isFresh());
  assert(factory.cameras[0].src_monitor_jpg.isFresh());
  assert(!factory.cameras[0].src_output_jpg.isFresh());
  jpg = factory.cameras[0].src_camera_jpg.peek();
  assert_headcam(jpg, 1);

  factory.setIdlePeriod(0.1d);
  cout << "idle: " << factory.getIdlePeriod() << endl;
  assert(0.1d == factory.getIdlePeriod());
  usleep(100000);
  assert(testProcess(0));
  assert(factory.cameras[0].src_camera_jpg.isFresh());
  assert(factory.cameras[0].src_camera_mat_gray.isFresh());
  assert(factory.cameras[0].src_camera_mat_bgr.isFresh());
  assert(!factory.cameras[0].src_monitor_jpg.isFresh());  // consumed by idle()
  assert(!factory.cameras[0].src_output_jpg.isFresh());
  jpg = factory.cameras[0].src_camera_jpg.peek();
  assert_headcam(jpg, 1);

  assert(testProcess(1));
  assert(!factory.cameras[0].src_camera_jpg.isFresh());
  assert(factory.cameras[0].src_camera_mat_gray.isFresh());
  assert(factory.cameras[0].src_camera_mat_bgr.isFresh());
  assert(factory.cameras[0].src_monitor_jpg.isFresh());
  assert(!factory.cameras[0].src_output_jpg.isFresh());
  jpg = factory.cameras[0].src_camera_jpg.peek();
  assert_headcam(jpg, 1);

  assert(testProcess(3));
  assert(factory.cameras[0].src_camera_jpg.isFresh());
  assert(factory.cameras[0].src_camera_mat_gray.isFresh()); 
  assert(factory.cameras[0].src_camera_mat_bgr.isFresh()); 
  assert(factory.cameras[0].src_monitor_jpg.isFresh());
  assert(!factory.cameras[0].src_output_jpg.isFresh());
  jpg = factory.cameras[0].src_camera_jpg.peek();
  assert_headcam(jpg, 0);

  Mat grayImage = factory.cameras[0].src_camera_mat_gray.get();
  cout << "grayImage: " << grayImage.rows << "x" << grayImage.cols << endl;
  assert(200 == grayImage.rows);
  assert(800 == grayImage.cols);
  assert(testProcess(2));
  assert(factory.cameras[0].src_camera_jpg.isFresh());
  assert(factory.cameras[0].src_camera_mat_gray.isFresh()); 
  assert(factory.cameras[0].src_camera_mat_bgr.isFresh()); // fresh but older than camera_jpg
  assert(factory.cameras[0].src_monitor_jpg.isFresh());
  assert(!factory.cameras[0].src_output_jpg.isFresh());
  jpg = factory.cameras[0].src_camera_jpg.peek();
  assert_headcam(jpg, 1);

  cout << "testCamera() PASS" << endl;
  cout << endl;
  return 0;
}

bool testString(const char * name, const char*expected, const char*actualValue) {
  if (strcmp(expected, actualValue)) {
    LOGTRACE3("TEST %s expected:\"%s\" actual:\"%s\"", name, expected, actualValue);
    return false;
  }
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

int testConfig() {
  cout << "testConfig() --------------------------" << endl;
  factory.clear();
  factory.processInit();
  const char *config_json = \
  "{ \"FireREST\":{\"title\":\"Raspberry Pi FireFUSE\",\"provider\":\"FireFUSE\", \"version\":{\"major\":0, \"minor\":6, \"patch\":0}},\n" \
    "\"cv\":{\n" \
      "\"cve_map\":{\n" \
	"\"one\":{ \"firesight\": [ {\"op\":\"putText\", \"text\":\"one\"} ], \"properties\": { \"caps\":\"ONE\" } },\n" \
	"\"two\":{ \"firesight\": [ {\"op\":\"putText\", \"text\":\"two\"} ], \"properties\": { \"caps\":\"TWO\" } }\n" \
      "},\n" \
      "\"camera_map\":{\n" \
	"\"1\":{ \"profile_map\":{ \"gray\":{ \"cve_names\":[ \"one\", \"two\" ] }, \"bgr\":{ \"cve_names\":[ \"one\", \"two\" ] }}}\n" \
      "}\n" \
    "}\n" \
  "}\n"; 


  assert(testString("TEST fuse_root", "/dev/firefuse", fuse_root));

  /////////// config test
  firerest_config(config_json);
  vector<string> cveNames = factory.getCveNames();
  cout << "cveNames.size(): " << cveNames.size() << endl;
  assert(4 == cveNames.size());
  cout << "cveNames[0]: " << cveNames[0] << endl;
  cout << "cveNames[1]: " << cveNames[1] << endl;
  cout << "cveNames[2]: " << cveNames[2] << endl;
  cout << "cveNames[3]: " << cveNames[3] << endl;
  assert(0==strcmp("/cv/1/bgr/cve/one", cveNames[0].c_str()));
  assert(0==strcmp("/cv/1/bgr/cve/two", cveNames[1].c_str()));
  assert(0==strcmp("/cv/1/gray/cve/one", cveNames[2].c_str()));
  assert(0==strcmp("/cv/1/gray/cve/two", cveNames[3].c_str()));
  SmartPointer<char> one_json(factory.cve("/cv/1/gray/cve/one").src_firesight_json.get());
  cout << one_json.data() << " " << one_json.size() << "B" << endl;
  assert(testString("firesight.json GET","[{\"op\":\"putText\",\"text\":\"one\"}]", one_json));
  SmartPointer<char> two_properties(factory.cve("/cv/1/bgr/cve/two").src_properties_json.get());
  cout << two_properties.data() << " " << two_properties.size() << "B" << endl;
  assert(0==strcmp("{\"caps\":\"TWO\"}", two_properties.data()));

  cout << "testConfig() PASS" << endl;
  cout << endl;
  return 0;
}

int testCve() {
  /////////// process.fire test
  cout << "testCve() --------------------------" << endl;
  factory.clear();
  factory.processInit();

  //////////// cveNames
  vector<string> cveNames = factory.getCveNames();
  assert(0 == cveNames.size());
  string firesightPath = "/cv/1/gray/cve/calc-offset/firesight.json";
  CVE& cve = factory.cve(firesightPath);
  cveNames = factory.getCveNames();
  assert(1 == cveNames.size());
  cout << "cveNames[0]: " << cveNames[0] << endl;
  assert(0 == strcmp("/cv/1/gray/cve/calc-offset", cveNames[0].c_str()));
  assert(0 == cveNames[0].compare(cve.getName()));

  /////////// firesight.json
  cout << firesightPath << " => " << (char *)cve.src_firesight_json.peek().data() << endl;
  const char * firesightJson = "[{\"op\":\"putText\", \"text\":\"CVE::CVE()\"}]";
  assert(factory.cve(firesightPath).src_firesight_json.isFresh());
  /* GET */ SmartPointer<char> firesight_json(factory.cve(firesightPath).src_firesight_json.get());
  assert(0==strcmp(firesightJson, firesight_json.data()));
  assert(!factory.cve(firesightPath).src_firesight_json.isFresh());
  assert(testProcess(0));
  assert(!factory.cve(firesightPath).src_firesight_json.isFresh()); // we don't refresh firesight.json in background
  assert(0==strcmp(firesightJson, factory.cve(firesightPath).src_firesight_json.get().data()));

  /////////// save.fire test
  assert(factory.cameras[0].src_camera_mat_gray.isFresh());
  assert(factory.cameras[0].src_camera_mat_bgr.isFresh());
  assert(factory.cve(firesightPath).src_save_fire.isFresh());
  assert(!factory.cameras[0].src_output_jpg.isFresh()); 
  assert(testNumber((size_t)0, factory.cameras[0].src_output_jpg.peek().size()));  
  assert(testString("save.fire BEFORE", "{}",factory.cve(firesightPath).src_save_fire.peek()));
  assert(testNumber((size_t) 0, factory.cve(firesightPath).src_saved_png.peek().size()));
  assert(factory.cve(firesightPath).src_save_fire.isFresh());
  /*GET*/ SmartPointer<char> save_fire(factory.cve(firesightPath).src_save_fire.get());
  assert(testString("save.fire GET", "{}",save_fire));
  assert(!factory.cve(firesightPath).src_save_fire.isFresh());
  assert(testProcess(1));
  assert(factory.cve(firesightPath).src_save_fire.isFresh());
  save_fire = factory.cve(firesightPath).src_save_fire.peek();
  assert(testString("save.fire processLoop ", "{\"bytes\":72944}", save_fire));
  assert(testNumber((size_t)72944, factory.cve(firesightPath).src_saved_png.peek().size()));
  SmartPointer<char> save_fire_contents = factory.cve(firesightPath).src_save_fire.peek();
  testFile("save.fire", "/cv/1/gray/cve/calc-offset/save.fire", save_fire_contents);
  assert(factory.cve(firesightPath).src_save_fire.isFresh()); // Verify async never triggered
  assert(factory.cameras[0].src_output_jpg.isFresh()); 
  assert(testNumber((size_t)39320, factory.cameras[0].src_output_jpg.peek().size()));  

  /////////// process.fire test
  assert(testString("process.fire BEFORE", "{}",factory.cve(firesightPath).src_process_fire.peek()));
  assert(factory.cve(firesightPath).src_process_fire.isFresh());
  assert(testNumber((size_t)39320, factory.cameras[0].src_output_jpg.peek().size()));
  /*GET*/ SmartPointer<char> process_fire(factory.cve(firesightPath).src_process_fire.get());
  assert(testNumber((size_t)39320, factory.cameras[0].src_output_jpg.peek().size()));
  assert(testString("process.fire GET", "{}",factory.cve(firesightPath).src_process_fire.peek()));
  assert(!factory.cve(firesightPath).src_process_fire.isFresh());
  /*ASYNC*/assert(testProcess(3)); // process + camera + mat_gray
  assert(testNumber((size_t) 40734, factory.cameras[0].src_output_jpg.peek().size()));
  assert(factory.cve(firesightPath).src_process_fire.isFresh());
  assert(testString("process.fire processLoop", "{\"s1\":{}}", factory.cve(firesightPath).src_process_fire.peek()));
  assert(testNumber((size_t)40734, factory.cameras[0].src_output_jpg.peek().size()));

  cout << "testCve() PASS" << endl;
  cout << endl;
  return 0;
}

int main(int argc, char *argv[]) {
  firelog_level(FIRELOG_TRACE);
  try {
    if (
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
