#include "LIFOCache.hpp"

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

int testLIFOCache() {
  cout << "testLIFOCache() ------------------------" << endl;
  {
    LIFOCache<MockValue<int> > bufInt;
    bufInt.post(MockValue<int>(1));

    cout << "testLIFOCache() get" << endl;
    assert(1 == bufInt.get().getValue());
    assert(1 == bufInt.get().getValue());
   
    cout << "testLIFOCache() post" << endl;
    bufInt.post(MockValue<int>(2));
    assert(2 == bufInt.get().getValue());
    bufInt.post(MockValue<int>(3));
    const char *caughtMsg = NULL;
    try {
      bufInt.post(MockValue<int>(4));
    } catch (const char * msg) {
      caughtMsg = msg;
    }
    assert(caughtMsg);

    LIFOCache<string> bufString;
    assert(!bufString.isFresh());
    bufString.post("one");
    assert(bufString.isFresh());
    assert(0 == strcmp("one", bufString.get().c_str()));
    assert(!bufString.isFresh());
    assert(0 == strcmp("one", bufString.get().c_str()));

    bufString.post("two");
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
      LIFOCache<CharPtr> bufCharPtr;
      assert(!bufCharPtr.isFresh());
      assert(NULL == (char *)bufCharPtr.get());
      assert(!bufCharPtr.isFresh());
      assert(0 == bufCharPtr.get().getReferences());
      bufCharPtr.post(spOne);
      assert(bufCharPtr.isFresh());
      assert(one == (char *)bufCharPtr.get());
      assert(!bufCharPtr.isFresh());
      assert(2 == bufCharPtr.get().getReferences());
      bufCharPtr.post(spTwo);
      assert(bufCharPtr.isFresh());
      assert(2 == spOne.getReferences());
      assert(2 == spTwo.getReferences());
      bufCharPtr.get();
      assert(1 == spOne.getReferences());
      assert(2 == spTwo.getReferences());
      bufCharPtr.post(spTwo);
      assert(3 == spTwo.getReferences());
      assert(bufCharPtr.isFresh());
      assert(two == (char *)bufCharPtr.get());
      assert(!bufCharPtr.isFresh());
      assert(2 == spTwo.getReferences());
      assert(2 == bufCharPtr.get().getReferences());
    }
    assert(0 != strcmp("one", one));
    assert(0 != strcmp("two", two));
  }

  cout << endl;
  cout << "testLIFOCache() PASS" << endl;
  cout << endl;

  return 0;
}

int main(int argc, char *argv[]) {
  return testSmartPointer()==0 && testLIFOCache()==0 ? 0 : -1;
}
