#ifndef KITLOG_H
#define KITLOG_H

#ifdef NDEBUG
    #define LOG(...)
    #define LOGFLUSH()
#else
    #include <stdio.h>
    #define LOG(...) fprintf(stderr, __VA_ARGS__)
    #define LOGFLUSH() fflush(stderr)
#endif

#endif // KITLOG_H
