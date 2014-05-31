#include <string.h>
#include <iostream>
#include <cassert>
#include <stdlib.h>

using namespace std;

template <class T> class DoubleBuffer {
protected:
  int valueCount;
  T values[2];

public:
  DoubleBuffer(T initialValue) {
    this->valueCount = 0;
    put(initialValue);
  }

  T get() {
    if (valueCount > 1) {
      values[0] = values[1];
      valueCount--;
    }
    return values[0];
  }

  void put(T value) {
    if (valueCount > 1) {
      throw "DoubleBuffer overflow";
    }
    values[valueCount] = value;
    valueCount++;
  }
};

template <class T> class DoubleBufferPtr : DoubleBuffer<T*> {
private:
  int isDelete;
  using DoubleBuffer<T*>::valueCount;
  using DoubleBuffer<T*>::values;

  void freeValue(int index) {
    if (isDelete) {
      cout << "DoubleBufferPtr delete " << (long) values[index] << endl;
      if (values[index]) {
	delete values[index];
      }
    } else {
      cout << "DoubleBufferPtr freeing " << (long) values[index] << endl;
      if (values[index]) {
	* (char *) values[index] = 0;
	free(values[index]);
      }
    }
  }

public:
  DoubleBufferPtr(T* initialValue, int isDelete=0) : DoubleBuffer<T*>(initialValue){
    this->valueCount = 0;
    this->isDelete = isDelete;
    put(initialValue);
  }

  ~DoubleBufferPtr() {
    for (int i = 0; i < valueCount; i++) {
      freeValue(i);
    }
  }
  
  T* get() {
    if (valueCount > 1) {
      freeValue(0);
      values[0] = values[1];
      valueCount--;
    }
    return values[0];
  }

  void put(T* value) {
    if (valueCount > 1) {
      throw "DoubleBuffer overflow";
    }
    values[valueCount] = value;
    valueCount++;
  }

};

template <class T> class SmartPointer {
  public: class ReferencedPointer {
    private: int references;
    private: T* ptr;

    public: ReferencedPointer() { 
      ptr = NULL;
      references = 0;
    }

    public: ReferencedPointer(T* ptr) {
      references = 1;
      this->ptr = ptr;
    }

    public: void decref() {
      references--;
      if (references < 0) {
	throw "ReferencedPointer extra dereference";
      }
      if (ptr && references == 0) {
	cout << "ReferencedPointer() free " << (long) ptr << endl;
	* (char *) ptr = 0; // mark as deleted
	free(ptr);
      }
    }

    public: void incref() { references++; }

    public: T* get() { return ptr; }

    public: int getReferences() { return references; }
  };

  private: ReferencedPointer *pPointer;

  public: SmartPointer(T* ptr) {
    cout << "SmartPointer(" << (long) ptr << ")" << endl;
    this->pPointer = new ReferencedPointer(ptr);
  }

  public: SmartPointer() {
    cout << "SmartPointer(NULL)" << endl;
    this->pPointer = NULL;
  }

  public: SmartPointer(const SmartPointer &that) {
    this->pPointer = that.pPointer;
    this->pPointer->incref();
  }

  public: ~SmartPointer() {
    if (pPointer) {
      pPointer->decref();
    }
  }

  public: SmartPointer& operator=( const SmartPointer& that ) {
    if (pPointer) {
      pPointer->decref();
    }
    this->pPointer = that.pPointer;
    this->pPointer->incref();
    return *this;
  }

  public: int getReferences() {
    return pPointer ? pPointer->getReferences() : 0;
  }

  public: T* operator->() const {
    return pPointer ? pPointer->get() : NULL;
  }

  public: operator T*() const {
    return pPointer ? pPointer->get() : NULL;
  }
};

class MockValue {
private:
  int value;

public:
  MockValue(int aValue) {
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

  int getValue() {
    cout << "MockValue.getValue() => " << value << " " << (long) this << endl;
    return value;
  }
};

int testSmartPointer( ){
  cout << "testSmartPointer() ------------------------" << endl;
  char *pOne = (char*)malloc(100);
  strcpy(pOne, "one");
  cout << "pOne == " << (long) pOne << endl;
  char *pTwo = (char*)malloc(100);
  strcpy(pTwo, "two");
  cout << "pTwo == " << (long) pTwo << endl;
  MockValue *pMock = new MockValue(123);
  cout << "pMock == " << (long) pMock << endl;
  {
    SmartPointer<char> zero;
    assert(NULL == (char*) zero);
    SmartPointer<char> one(pOne);
    assert(pOne == (char*)one);
    assert(*pOne == 'o');
    assert(1 == one.getReferences());
    assert(0 == strcmp("one", (char*)one));
    assert(0 == strcmp("one", pOne));
    SmartPointer<char> oneCopy(one);
    assert(0 == strcmp("one", (char*)oneCopy));
    assert(2 == one.getReferences());
    assert(2 == oneCopy.getReferences());
    SmartPointer<char> two(pTwo);
    assert(0 == strcmp("two", (char*)two));
    assert(0 != strcmp((char *) one, (char *) two));
    one = two;
    assert(0 == strcmp((char *) one, (char *) two));
    assert(2 == one.getReferences());
    assert(1 == oneCopy.getReferences());

    SmartPointer<MockValue> mock(pMock);
    assert(123 == mock->getValue());
  }
  assert(0 != strcmp("one", pOne));
  assert(0 != strcmp("two", pTwo));
  cout << "testSmartPointer() PASSED" << endl;
  cout << endl;

  return 0;
}

int testDoubleBuffer() {
  cout << "testDoubleBuffer() ------------------------" << endl;
  {
    DoubleBuffer<MockValue> bufInt(MockValue(1));

    cout << "testDoubleBuffer() get" << endl;
    assert(1 == bufInt.get().getValue());
    assert(1 == bufInt.get().getValue());
   
    cout << "testDoubleBuffer() put" << endl;
    bufInt.put(MockValue(2));
    assert(2 == bufInt.get().getValue());
    bufInt.put(MockValue(3));
    const char *caughtMsg = NULL;
    try {
      bufInt.put(MockValue(4));
    } catch (const char * msg) {
      caughtMsg = msg;
    }
    assert(caughtMsg);

    DoubleBuffer<string> bufString("one");
    assert(0 == strcmp("one", bufString.get().c_str()));
    assert(0 == strcmp("one", bufString.get().c_str()));

    bufString.put("two");
    assert(0 == strcmp("two", bufString.get().c_str()));

    char *one = (char *) malloc(100);
    strcpy(one, "one");
    char *two = (char *) malloc(200);
    strcpy(two, "two");
    {
      DoubleBufferPtr<char> bufAlloc(NULL, 0);
      assert(!bufAlloc.get());
      bufAlloc.put(one);
      assert(0 == strcmp("one", bufAlloc.get()));
      assert(0 == strcmp("one", bufAlloc.get()));
      bufAlloc.put(two);
      assert(0 == strcmp("two", bufAlloc.get()));
    }
    assert(0 == strlen(one));
    assert(0 == strlen(two));
  }

  cout << endl;
  cout << "testDoubleBuffer() PASS" << endl;
  cout << endl;

  return 0;
}

int main(int argc, char *argv[]) {
  return testSmartPointer()==0 && testDoubleBuffer()==0 ? 0 : -1;
}
