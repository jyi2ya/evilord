#ifndef PACKET_H_
#define PACKET_H_

#include <stdint.h>
/**
 * Packet - 进行运算的单位数据
 *
 * 本算法对数据的存储皆通过 Packet 完成。
 *
 * Packet 可以为任意类型，只要其正确实现 PXOR PZERO PASGN 三个宏。
 *
 * 目前所采用的 Packet 为 uint64_t 类型。测试发现使用 __uint128_t 类型时，性能
 * 无明显改观（在超快的集群上对性能提升仅有 5.4%），且可能引用潜在的对编译器版
 * 本的依赖的问题。故目前使用 uint64_t。
 */
typedef uint64_t Packet;

/**
 * PXOR() - 异或两个 Packet，并且保存到前者
 */
#define PXOR(x, y) do { \
    (x) ^= (y); \
} while (0)

/**
 * PZERO() - 将某个 Packet 置零
 */
#define PZERO(x) do { \
    (x) = 0; \
} while (0)

/**
 * PASGN() - Packet 赋值操作
 */
#define PASGN(x, y) do { \
    (x) = (y); \
} while (0)

/**
 * PMAX - evenodd 算法所使用的质数的最大值
 */
#define PMAX (201)

#endif
