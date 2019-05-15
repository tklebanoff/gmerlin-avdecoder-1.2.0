#ifndef __BGAV_VERSION_H_
#define __BGAV_VERSION_H_

#define BGAV_VERSION "1.2.0"

#define BGAV_VERSION_MAJOR 1
#define BGAV_VERSION_MINOR 2
#define BGAV_VERSION_MICRO 0

#define BGAV_MAKE_BUILD(a,b,c) ((a << 16) + (b << 8) + c)

#define BGAV_BUILD \
BGAV_MAKE_BUILD(BGAV_VERSION_MAJOR, \
                BGAV_VERSION_MINOR, \
                BGAV_VERSION_MICRO)

#endif // __BGAV_VERSION_H_
