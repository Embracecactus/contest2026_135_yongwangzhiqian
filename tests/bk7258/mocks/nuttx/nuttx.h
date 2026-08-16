#ifndef __MOCK_NUTTX_NUTTX_H
#define __MOCK_NUTTX_NUTTX_H

#include <stddef.h>

#ifndef FAR
#  define FAR
#endif

#ifndef container_of
#  define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#endif

#endif /* __MOCK_NUTTX_NUTTX_H */
