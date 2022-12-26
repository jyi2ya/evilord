#ifndef UTIL_H_
#define UTIL_H_

#if defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7))
#define UNUSED_PARAM __attribute__((unused))
#define UNUSED_PARAM_RESULT __attribute__((warn_unused_result))
#else // Non-GCC or old GCC.
#define UNUSED_PARAM
#define UNUSED_PARAM_RESULT
#endif

/**
 * M() - 对 m 取模
 */
#define M(x) (((x) + m) % m)
#define MIN(x, y) ((x) < (y) ? (x) : (y))

#endif
