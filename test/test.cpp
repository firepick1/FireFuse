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

  return 0;
}

int main(int argc, char *argv[]) {
  return testDoubleBuffer();
}
