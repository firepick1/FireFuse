#include <string.h>
#include "FireLog.h"
using namespace std;

template <class T> public class DoubleBuffer {
  private int valueCount = 0;
  private  T values[2];

  DoubleBuffer(T initialValue) {
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
      LOGERROR("DoubleBuffer overflow");
      throw "DoubleBuffer overflow";
    }
    values[valueCount] = value;
    valueCount++;
  }
}

public class RefInt {
  int value;
  int refCnt;

  RefInt(int aValue) {
    value = aValue;
    refCnt = 1;
  }

  ~RefInt() {
    LOGINFO2("~RefInt(%d) refCnt:%d", value, refCnt);
    refCnt--;
  }

  int getValue() {
    LOGINFO1("RefInt.getValue() => %d", value);
    return value;
  }
}

int testDoubleBuffer() {
  DoubleBuffer<RefInt> buf(RefInt(1));

  assert(1 == buf.getValue());
  assert(1 == buf.getValue());
  buf.putValue(RefInt(2));
  assert(2 == buf.getValue());
  buf.putValue(RefInt(3));
  char *caughtMsg = NULL;
  try {
    buf.putValue(RefInt(4));
  } catch (const char * msg) {
    caughtMsg = msg;
  }
  assert(caughtMsg);

  return 0;
}

int main(int argc, char *argv[]) {
  return testDoubleBuffer();
}
