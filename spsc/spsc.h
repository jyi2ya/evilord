#include <stdlib.h>

#if defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7))
#define read_barrier() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define write_barrier() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#else // Non-GCC or old GCC.
#if defined(__x86_64__) || defined(_M_X64_) || defined(_M_I86) || defined(__i386__)
#define read_barrier()  __asm__ volatile("":::"memory")
#define write_barrier() __asm__ volatile("":::"memory")
#elif defined(__arm__) || defined(__aarch64__)
#define read_barrier() __asm__ volatile("dmb sy")
#define write_barrier() __asm__ volatile("dmb sy")
#endif
#endif

typedef void *ItemType;

typedef struct {
    volatile unsigned int in;
    volatile unsigned int out;
    unsigned int mask;
    ItemType * volatile data;
    struct {
        size_t wait;
        size_t cnt;
    } push, pop;
} SpscQueue;

SpscQueue SpscQueue_new(unsigned int size);

void SpscQueue_drop(SpscQueue *self);

int SpscQueue_empty(SpscQueue *self);

void SpscQueue_push(SpscQueue *self, ItemType data);

int SpscQueue_full(SpscQueue *self);

ItemType SpscQueue_pop(SpscQueue *self);

void SpscQueue_perf(SpscQueue *self, const char *prompt);
