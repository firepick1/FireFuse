#ifndef LIFOCache_HPP
#define LIFOCache_HPP

#include <string.h>
#include <iostream>
#include <cassert>
#include <stdlib.h>
#include <memory>
#include <sched.h>

using namespace std;

#ifndef bool
#define bool int
#endif
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

/**
 * Threadsafe LIFO single-producer/multi-consumer cache that returns most recently posted value.
 */
template <class T> class LIFOCache {
  private: volatile long readCount;
  private: volatile long writeCount;
  private: T emptyValue;
  private: T values[2];
  private: volatile int nReaders;

  public: LIFOCache() { 
    this->readCount = 0;
    this->writeCount = 0;
    this->nReaders = 0;
  }

  public: ~LIFOCache() { }

  public: T& get() {
    int valueIndex = writeCount - readCount;
    if (valueIndex > 0) {
      ///////////// CRITICAL SECTION BEGIN ///////////////
      while (++nReaders != 1) {				//
        --nReaders;					//
	sched_yield();					//
      }							//
      values[0] = values[valueIndex];			//
      for (int i = 1; i <= valueIndex; i++) {		//
	values[i] = emptyValue;				//
      }							//
      nReaders--; 					//
      ///////////// CRITICAL SECTION END /////////////////
    }
    readCount = writeCount;
    return values[0];
  }

  public: void post(T value) {
    int valueIndex = writeCount - readCount + 1;
    if (valueIndex >= 2) {
      throw "LIFOCache overflow";
    }
    values[valueIndex] = value;
    writeCount++;
  }

  public: bool isFresh() { return writeCount && writeCount != readCount; }
};

template <class T> class SmartPointer {
  public: class ReferencedPointer {
    private: int references;
    private: T* ptr;

    public: inline ReferencedPointer() { 
      ptr = NULL;
      references = 0;
    }

    public: inline ReferencedPointer(T* ptr) {
      references = 1;
      this->ptr = ptr;
    }

    public: inline void decref() {
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

    public: inline void incref() { references++; }
    public: inline T* get() { return ptr; }
    public: inline int getReferences() const { return references; }
  };

  private: ReferencedPointer *pPointer;
  private: inline void decref() { if (pPointer) { pPointer->decref(); } }
  private: inline void incref() { if (pPointer) { pPointer->incref(); } }

  public: inline SmartPointer(T* ptr) {
    cout << "SmartPointer(" << (long) ptr << ")" << endl;
    this->pPointer = new ReferencedPointer(ptr);
  }

  public: inline SmartPointer() {
    cout << "SmartPointer(NULL)" << endl;
    this->pPointer = NULL;
  }

  public: inline SmartPointer(const SmartPointer &that) {
    this->pPointer = that.pPointer;
    this->incref();
  }

  public: inline ~SmartPointer() { decref(); }

  public: inline SmartPointer& operator=( SmartPointer that ) {
    decref();
    this->pPointer = that.pPointer;
    incref();
    return *this;
  }

  public: inline int getReferences() { return pPointer ? pPointer->getReferences() : 0; }
  public: inline T& operator*() { throw "SmartPointer not implemented (1)"; }
  public: inline const T& operator*() const { throw "SmartPointer not implemented (2)"; }
  public: inline T* operator->() { return pPointer ? pPointer->get() : NULL; }
  public: inline const T* operator->() const { return pPointer ? pPointer->get() : NULL; }
  public: inline operator T*() const { return pPointer ? pPointer->get() : NULL; }
};

#endif
