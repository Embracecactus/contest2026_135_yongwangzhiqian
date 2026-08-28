/****************************************************************************
 * tests/mocks/nuttx/compiler.h
 *
 * Host shim for the NuttX compiler macros used by board/chip headers.
 * Only the surface consumed by the AP board helper headers is provided.
 ****************************************************************************/

#ifndef __MOCK_NUTTX_COMPILER_H
#define __MOCK_NUTTX_COMPILER_H

#ifndef FAR
#define FAR
#endif

#ifndef UNUSED
#define UNUSED(x) ((void)(x))
#endif

#ifndef noreturn_function
#  define noreturn_function __attribute__((noreturn))
#endif

#if !defined(__cplusplus) && !defined(static_assert)
#  define static_assert _Static_assert
#endif

#ifndef nitems
#  define nitems(a) (sizeof(a) / sizeof((a)[0]))
#endif

#endif /* __MOCK_NUTTX_COMPILER_H */
