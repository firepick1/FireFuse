#ifndef CACHEDVALUE_HPP
#define CACHEDVALUE_HPP

#include <string.h>
#include <iostream>
#include <cassert>
#include <stdlib.h>
#include <memory>

using namespace std;

template <class T> class CachedValue {
  private: int valueCount;
  private: T emptyValue;
  private: T values[2];

  public: CachedValue() { this->valueCount = 0; }

  public: T& get() {
    if (valueCount > 1) {
      values[0] = values[1];
      values[1] = emptyValue;
      valueCount--;
    }
    return values[0];
  }

  public: void put(T value) {
    if (valueCount > 1) {
      throw "CachedValue overflow";
    }
    values[valueCount] = value;
    valueCount++;
  }

  public: int getValueCount() { return valueCount; }
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
