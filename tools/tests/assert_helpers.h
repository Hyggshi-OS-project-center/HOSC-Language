#ifndef ASSERT_HELPERS_H
#define ASSERT_HELPERS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT_EQ_INT(a, b) do { \
    long long _a = (long long)(a); \
    long long _b = (long long)(b); \
    if (_a != _b) { \
        fprintf(stderr, "Assertion failed: %s == %s (%lld != %lld) at %s:%d\n", \
                #a, #b, _a, _b, __FILE__, __LINE__); \
        abort(); \
    } \
} while(0)

#define ASSERT_STR_EQ(a, b) do { \
    const char *_sa = (a); \
    const char *_sb = (b); \
    if ((_sa == NULL && _sb != NULL) || (_sa != NULL && _sb == NULL) || \
        (_sa != NULL && _sb != NULL && strcmp(_sa, _sb) != 0)) { \
        fprintf(stderr, "Assertion failed: %s == %s (\"%s\" != \"%s\") at %s:%d\n", \
                #a, #b, _sa ? _sa : "NULL", _sb ? _sb : "NULL", __FILE__, __LINE__); \
        abort(); \
    } \
} while(0)

#define ASSERT_NOT_NULL(p) do { \
    if ((p) == NULL) { \
        fprintf(stderr, "Assertion failed: %s is NULL at %s:%d\n", #p, __FILE__, __LINE__); \
        abort(); \
    } \
} while(0)

#define ASSERT_NULL(p) do { \
    if ((p) != NULL) { \
        fprintf(stderr, "Assertion failed: %s is not NULL at %s:%d\n", #p, __FILE__, __LINE__); \
        abort(); \
    } \
} while(0)

#endif
