#ifndef LIFOCache_HPP
#define LIFOCache_HPP

#include <string.h>
#include <iostream>
#include <cassert>
#include <stdlib.h>
#include <memory>
#include <sched.h>
#include <semaphore.h>
#include <FireLog.h>

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
    private:
        volatile long readCount;
    private:
        volatile long writeCount;
    private:
        volatile long syncCount;
    private:
        T values[2];
    private:
        pthread_mutex_t readerMutex;
    private:
        sem_t getSem;

    public:
        LIFOCache() {
            this->readCount = 0;
            this->writeCount = 0;
            this->syncCount = 0;
            int rc_readerMutex = pthread_mutex_init(&readerMutex, NULL);
            assert(rc_readerMutex == 0);
            int rc_getSem = sem_init(&getSem, 0, 0);
            assert(rc_getSem == 0);
        }

    public:
        ~LIFOCache() {
            int rc = pthread_mutex_destroy(&readerMutex);
            assert(rc == 0);
        }

    public:
        T peek() {
            /////////////// CRITICAL SECTION BEGIN ///////////////
            pthread_mutex_lock(&readerMutex);			
            int valueIndex = writeCount - readCount;
            T result;						
            if (valueIndex > 0) {		
                valueIndex = 1;		
                result = values[valueIndex];			
            } else {						
                result = values[0];		
            }						
            pthread_mutex_unlock(&readerMutex);			
            /////////////// CRITICAL SECTION END /////////////////
            return result;
        }

        // Cached get
    public:
        T get() {
            /////////////// CRITICAL SECTION BEGIN ///////////////
            pthread_mutex_lock(&readerMutex);		
            int valueIndex = writeCount - readCount;
            if (valueIndex > 0) {				
                valueIndex = 1;				
                values[0] = values[valueIndex];
            }							
            readCount = writeCount;	
            T result = values[0];
            pthread_mutex_unlock(&readerMutex);			
            /////////////// CRITICAL SECTION END /////////////////
            return result;
        }

    public:
        T get_sync(int msTimeout=0) {
            /////////////// CRITICAL SECTION BEGIN ///////////////
            pthread_mutex_lock(&readerMutex);		
            readCount = writeCount;				
            syncCount++;					
            pthread_mutex_unlock(&readerMutex);			
            /////////////// CRITICAL SECTION END /////////////////
            struct timespec ts;
            int rc = sem_trywait(&getSem);
            if (rc) {
                LOGDEBUG1("LIFOCache::get_sync() Waiting for queue input. timeout:%dms", msTimeout);
                if (msTimeout==0 || clock_gettime(CLOCK_REALTIME, &ts) == -1) {
                    rc = sem_wait(&getSem);
                    if (rc) {
                        throw "get_sync() sem_wait failed";
                    }
                } else {
                    long long int ns = ts.tv_nsec;
                    ns += msTimeout * 1000000l;
                    ts.tv_nsec = ns % 1000000000l;
                    ts.tv_sec += ns / 1000000000l;
                    rc = sem_timedwait(&getSem, &ts);
                    if (rc) {
						LOGERROR1("get_sync() %dms TIMEOUT EXCEEDED", msTimeout);
                    }
                }
            } else {
                LOGWARN1("LIFOCache::get_sync(%d) succeeded immediately", msTimeout);
			}

            T result = get();
            return result;
        }

    public:
        void post(T value) {
            bool postGetSem = FALSE;
            /////////////// CRITICAL SECTION BEGIN ///////////////
            pthread_mutex_lock(&readerMutex);			
            int valueIndex = writeCount - readCount + 1;
            if (valueIndex >= 2) {				
                valueIndex = 1; // overwrite existing		
            }							
            values[valueIndex] = value;	
            writeCount++;			
            if (syncCount > 0) {
                syncCount--;
                postGetSem = TRUE;				
            }							
            pthread_mutex_unlock(&readerMutex);			
            /////////////// CRITICAL SECTION END /////////////////
            if (postGetSem) {
                sem_post(&getSem);
            }
        }

    public:
        bool isFresh() {
            return writeCount && writeCount != readCount;
        }

    public:
        long getWriteCount() {
            return writeCount;
        }
    public:
        long getReadCount() {
            return readCount;
        }
};

template <class T> class SmartPointer {
    private:
        class ReferencedPointer {
            private:
                volatile int references;
            private:
                T* volatile ptr;
            private:
                size_t length;
            private:
                size_t allocated_length;

            public:
                inline ReferencedPointer() {
                    this->ptr = NULL;
                    this->references = 0;
                    this->length = 0;
                    this->allocated_length = 0;
                }

            public:
                inline ReferencedPointer(T* aPtr, size_t length) {
                    ptr = aPtr;
                    references = 1;
                    this->length = length;
                    this->allocated_length = length;
                    LOGTRACE1("ReferencedPointer(%0lx) managing allocated memory", (ulong) ptr);
                }

            public:
                inline void decref() {
                    references--;
                    if (references < 0) {
                        LOGERROR1("ReferencedPointer::decref(%0lx) extra derefence", (ulong) ptr);
                        throw "ReferencedPointer::decref() extra dereference";
                    }
                    if (ptr && references == 0) {
                        LOGTRACE1("ReferencedPointer::decref(%0lx) free", (ulong) ptr);
                        // Comment out the following to determine if memory is accessed after being freed
                        ///////////// FREE BEGIN
                        * (char *) ptr = 0; // mark as deleted
                        free(ptr);
                        ///////////// FREE END
                    }
                }

            public:
                inline void incref() {
                    references++;
                }
            public:
                inline T* data() {
                    return ptr;
                }
            public:
                inline size_t size() {
                    return length;
                }
            public:
                inline size_t allocated_size() {
                    return allocated_length;
                }
            public:
                inline int getReferences() const {
                    return references;
                }
            public:
                inline void setSize(size_t value) {
                    if (length > allocated_length) {
                        throw "SmartPointer::ReferencedPointer::setSize() exceeds allocated size";
                    }
                    length = value;
                }
        };

    public:
        enum { MANAGE, ALLOCATE };
    private:
        ReferencedPointer *pPointer;
    private:
        inline void decref() {
            if (pPointer) {
                pPointer->decref();
                //LOGTRACE2("SmartPointer(%0lx) decref:%d", pPointer->data(), pPointer->getReferences());
            }
        }
    private:
        inline void incref() {
            if (pPointer) {
                pPointer->incref();
                //LOGTRACE2("SmartPointer(%0lx) incref:%d", pPointer->data(), pPointer->getReferences());
            }
        }

        /**
         * Create a smart pointer for the given data. Copied SmartPointers
         * share the same data block, which is freed when all SmartPointers
         * referencing that data are destroyed. Clients can provide data to
         * be managed (i.e., count==0) or have SmartPointer allocate new memory initialized
         * from the provided data (i.e., count>0). Managed data will eventually be freed
         * by SmartPointer. Initialization data will not be freed by SmartPointer.
         *
         * @param aPtr pointer to data. If ptr is null, count must be number of objects to calloc and zero-fill
         * @param count number of T objects to calloc for data copied from ptr
         * @param flags ALLOCATE new memory or MANAGE memory to free()
         * @param blockSize byte data increment for self-describing data (ALLOCATE)
         * @param blockPad block byte fill value (ALLOCATE)
         */
    public:
        inline SmartPointer(T* aPtr, size_t count=0, int flags=ALLOCATE, size_t blockSize=1, char blockPad=0) {
            size_t length = count * sizeof(T);
            if (count && flags == ALLOCATE) {
                size_t blocks = (count*sizeof(T) + blockSize - 1)/blockSize;
                T* pData = (T*) calloc(blocks, blockSize);
                size_t blockBytes = blocks * blockSize;
                if (aPtr) {
                    memset(pData+length, blockPad, blockBytes-length);
                    memcpy(pData, aPtr, length);
                } else {
                    memset(pData, blockPad, blockBytes);
                }
                LOGTRACE3("SmartPointer(%0lx,%ld) calloc:%0lx", (ulong) aPtr, (ulong) count, (ulong) pData);
                pPointer = new ReferencedPointer(pData, blockBytes);
            } else {
                LOGTRACE2("SmartPointer(%0lx,%ld)", (ulong) aPtr, (ulong) count);
                pPointer = aPtr ? new ReferencedPointer(aPtr, length) : NULL;
            }
        }

    public:
        inline SmartPointer() {
            pPointer = NULL;
        }

    public:
        inline SmartPointer(const SmartPointer &that) {
            pPointer = that.pPointer;
            incref();
        }

    public:
        inline ~SmartPointer() {
            decref();
        }

    public:
        inline SmartPointer& operator=( SmartPointer that ) {
            that.incref();
            decref();
            pPointer = that.pPointer;
            return *this;
        }

    public:
        inline T* data() const {
            T* pData = pPointer ? pPointer->data() : NULL;
            return pData;
        }

    public:
        inline int getReferences() {
            return pPointer ? pPointer->getReferences() : 0;
        }
    public:
        inline T& operator*() {
            throw "SmartPointer not implemented (1)";
        }
    public:
        inline const T& operator*() const {
            throw "SmartPointer not implemented (2)";
        }
    public:
        inline T* operator->() {
            return pPointer ? pPointer->data() : NULL;
        }
    public:
        inline const T* operator->() const {
            return pPointer ? pPointer->data() : NULL;
        }
    public:
        inline operator T*() const {
            return pPointer ? pPointer->data() : NULL;
        }
    public:
        inline size_t size() const {
            return pPointer ? pPointer->size() : 0;
        }
    public:
        inline void setSize(size_t value) {
            if (!pPointer) {
                throw "cannot set size on NULL SmartPointer";
            }
            pPointer->setSize(value);
        }
    public:
        inline size_t allocated_size() const {
            return pPointer ? pPointer->allocated_size() : 0;
        }
};

#endif
