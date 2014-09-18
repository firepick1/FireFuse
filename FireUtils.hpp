/*

The MIT License (MIT)

Copyright (c) 2014 Karl Lew https://github.com/firepick1

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*/


#ifndef TINYASSERT_HPP
#define TINYASSERT_HPP

#include <assert.h>
#include <iostream>
#include "FireLog.h"
#include "string.h"

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif
#ifndef bool
#define bool int
#endif

inline int fail(int rc) {
    std::cout << "***ASSERT FAILED*** expected:0 actual:" << rc << std::endl;
    return FALSE;
}
#define ASSERTNONZERO(exp) assertnonzero((long) exp, __FILE__, __LINE__)
#define ASSERTZERO(exp) assertzero((long) exp, __FILE__, __LINE__)
#define ASSERT(e) ASSERTNONZERO(e)

inline void
assertzero(long actual, const char* fname, long line) {
    if (actual==0) {
        return;
    }

    char buf[255];
    snprintf(buf, sizeof(buf), "%s@%ld expected zero", fname, line);
    LOGERROR(buf);
    std::cerr << "***ASSERT FAILED*** " << buf << std::endl;
    assert(false);
}

inline void
assertnonzero(long actual, const char* fname, long line) {
    if (actual) {
        return;
    }

    char buf[255];
    snprintf(buf, sizeof(buf), "%s@%ld expected non-zero", fname, line);
    LOGERROR(buf);
    std::cerr << "***ASSERT FAILED*** " << buf << std::endl;
    assert(false);
}

#define ASSERTEQUAL(e,a) assertEqual((double)e,(double)a,0,__FILE__,__LINE__)
#define ASSERTEQUALT(e,a,t) assertEqual(e,a,t,__FILE__,__LINE__)
inline void
assertEqual(double expected, double actual, double tolerance, const char* context, long line)
{
    double diff = expected - actual;
    if (-tolerance <= diff && diff <= tolerance) {
        return;
    }

    char buf[255];
    snprintf(buf, sizeof(buf), "%s expected:%g actual:%g tolerance:%g line:%ld",
             context, expected, actual, tolerance, line);
    LOGERROR(buf);
    std::cerr << "***ASSERT FAILED*** " << buf << std::endl;
    assert(false);
}

#define ASSERTEQUALS(e,a) assertEqual(e,a,__FILE__,__LINE__)
inline void
assertEqual(const char* expected, const char* actual, const char* context, int line) {
    if (actual && strcmp(expected, actual)==0) {
        return;
    }

    char buf[255];
    if (actual) {
        snprintf(buf, sizeof(buf), "%s@%d expected:\"%s\" actual:\"%s\"", context, line, expected, actual);
    } else {
        snprintf(buf, sizeof(buf), "%s@%d expected:\"%s\" actual:NULL", context, line, expected);
    }
    LOGERROR(buf);
    std::cerr << "***ASSERT FAILED*** " << buf << std::endl;
    assert(false);
}

#endif
