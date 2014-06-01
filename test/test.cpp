#include <string.h>
#include <iostream>
#include <cassert>
#include <stdlib.h>
#include <memory>

using namespace std;

template <class T> class CachedValue {
protected:
  int valueCount;
  T emptyValue;
  T values[2];

public:
  CachedValue() {
    this->valueCount = 0;
  }

  T& get() {
    if (valueCount > 1) {
      values[0] = values[1];
      values[1] = emptyValue;
      valueCount--;
    }
    return values[0];
  }

  void put(T value) {
    if (valueCount > 1) {
      throw "CachedValue overflow";
    }
    values[valueCount] = value;
    valueCount++;
  }

  int getValueCount() {
    return valueCount;
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

    public: int getReferences() const { 
      return references; 
    }
  };

  private: ReferencedPointer *pPointer;
  private: void decref() { if (pPointer) { pPointer->decref(); } }
  private: void incref() { if (pPointer) { pPointer->incref(); } }

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
    this->incref();
  }

  public: ~SmartPointer() {
    decref();
  }

  public: SmartPointer& operator=( SmartPointer that ) {
    decref();
    this->pPointer = that.pPointer;
    incref();
    return *this;
  }

  public: int getReferences() {
    return pPointer ? pPointer->getReferences() : 0;
  }

  public: T& operator*() {
    throw "SmartPointer not implemented (1)";
  }

  public: const T& operator*() const {
    throw "SmartPointer not implemented (2)";
  }

  public: T* operator->() {
    return pPointer ? pPointer->get() : NULL;
  }

  public: const T* operator->() const {
    return pPointer ? pPointer->get() : NULL;
  }

  public: operator T*() const {
    return pPointer ? pPointer->get() : NULL;
  }
};

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
};

//#define SMARTPOINTER shared_ptr
#define SMARTPOINTER SmartPointer

int testSmartPointer( ){
  cout << "testSmartPointer() ------------------------" << endl;
  char *pOne = (char*)malloc(100);
  strcpy(pOne, "one");
  cout << "pOne == " << (long) pOne << endl;
  char *pTwo = (char*)malloc(100);
  strcpy(pTwo, "two");
  cout << "pTwo == " << (long) pTwo << endl;
  MockValue<int> *pMock = new MockValue<int>(123);
  cout << "pMock == " << (long) pMock << endl;
  {
    SMARTPOINTER<char> zero;
    assert(NULL == (char*) zero);
    SMARTPOINTER<char> one(pOne);
    assert(pOne == (char*)one);
    assert(*pOne == 'o');
    assert(1 == one.getReferences());
    assert(0 == strcmp("one", (char*)one));
    assert(0 == strcmp("one", pOne));
    SMARTPOINTER<char> oneCopy(one);
    assert(0 == strcmp("one", (char*)oneCopy));
    assert(2 == one.getReferences());
    assert(2 == oneCopy.getReferences());
    SMARTPOINTER<char> two(pTwo);
    assert(0 == strcmp("two", (char*)two));
    assert(0 != strcmp((char *) one, (char *) two));
    one = two;
    assert(0 == strcmp((char *) one, (char *) two));
    assert(2 == one.getReferences());
    assert(1 == oneCopy.getReferences());

    SMARTPOINTER<MockValue<int> > mock(pMock);
    assert(123 == mock->getValue());
  }
  assert(0 != strcmp("one", pOne));
  assert(0 != strcmp("two", pTwo));
  cout << "testSmartPointer() PASSED" << endl;
  cout << endl;

  return 0;
}

typedef SmartPointer<char> CharPtr;

int testCachedValue() {
  cout << "testCachedValue() ------------------------" << endl;
  {
    CachedValue<MockValue<int> > bufInt;
    bufInt.put(MockValue<int>(1));

    cout << "testCachedValue() get" << endl;
    assert(1 == bufInt.get().getValue());
    assert(1 == bufInt.get().getValue());
   
    cout << "testCachedValue() put" << endl;
    bufInt.put(MockValue<int>(2));
    assert(2 == bufInt.get().getValue());
    bufInt.put(MockValue<int>(3));
    const char *caughtMsg = NULL;
    try {
      bufInt.put(MockValue<int>(4));
    } catch (const char * msg) {
      caughtMsg = msg;
    }
    assert(caughtMsg);

    CachedValue<string> bufString;
    bufString.put("one");
    assert(0 == strcmp("one", bufString.get().c_str()));
    assert(0 == strcmp("one", bufString.get().c_str()));
    assert(1 == bufString.getValueCount());

    bufString.put("two");
    assert(0 == strcmp("two", bufString.get().c_str()));

    char *one = (char *) malloc(100);
    strcpy(one, "one");
    cout << "one " << (long) one << endl;
    char *two = (char *) malloc(200);
    cout << "two " << (long) two << endl;
    strcpy(two, "two");
    {
      CharPtr spOne(one);
      assert(1 == spOne.getReferences());
      CharPtr spTwo(two);
      CachedValue<CharPtr> bufCharPtr;
      assert(0 == bufCharPtr.getValueCount());
      assert(NULL == (char *)bufCharPtr.get());
      assert(0 == bufCharPtr.get().getReferences());
      bufCharPtr.put(spOne);
      assert(1 == bufCharPtr.getValueCount());
      assert(one == (char *)bufCharPtr.get());
      assert(2 == bufCharPtr.get().getReferences());
      bufCharPtr.put(spTwo);
      assert(2 == bufCharPtr.getValueCount());
      assert(two == (char *)bufCharPtr.get());
      assert(1 == bufCharPtr.getValueCount());
      assert(2 == bufCharPtr.get().getReferences());
    }
    assert(0 != strcmp("one", one));
    assert(0 != strcmp("two", two));
  }

  cout << endl;
  cout << "testCachedValue() PASS" << endl;
  cout << endl;

  return 0;
}

int main(int argc, char *argv[]) {
  return testSmartPointer()==0 && testCachedValue()==0 ? 0 : -1;
}
