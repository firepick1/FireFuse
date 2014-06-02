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
  void setValue(T value) {
    this->value = value;
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
  MockValue<int> *pMock = new MockValue<int>(123);
  cout << "pMock == " << (long) pMock << endl;
  {
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
  }
  assert(0 != strcmp("one", pOne));
  assert(0 != strcmp("two", pTwo));
  cout << "testSmartPointer() PASSED" << endl;
  cout << endl;

  return 0;
}

int testSmartPointer_CopyData( ){
  cout << "testSmartPointer_CopyData() ------------------------" << endl;
  char *pOne = (char*)malloc(123456);
  strcpy(pOne, "one");
  cout << "pOne == " << (long) pOne << endl;
  char *pTwo = (char*)malloc(222222);
  strcpy(pTwo, "two");
  cout << "pTwo == " << (long) pTwo << endl;
  MockValue<int> *pMock = new MockValue<int>(123);
  cout << "pMock == " << (long) pMock << endl;
  {
    SmartPointer<char> zero;
    assert(NULL == (char*) zero);
    assert(0 == zero.size());
    SmartPointer<char> one(pOne, 123456);		// COPY DATA ONLY
    assert(123456 == one.size());
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
  }
  assert(0 != strcmp("one", pOne));
  assert(0 != strcmp("two", pTwo));
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
    assert(0 != strcmp("one", one));
    assert(0 != strcmp("two", two));
  }

  cout << endl;
  cout << "testLIFOCache() PASS" << endl;
  cout << endl;

  return 0;
}

int main(int argc, char *argv[]) {
  return testSmartPointer()==0 && testSmartPointer_CopyData()==0 && testLIFOCache()==0 ? 0 : -1;
}
