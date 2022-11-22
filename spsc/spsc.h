#include <stdlib.h>

#define read_barrier()  __asm__ volatile("":::"memory")
#define write_barrier() __asm__ volatile("":::"memory")

typedef void *ItemType;

typedef struct {
    volatile unsigned int in;
    volatile unsigned int out;
    unsigned int mask;
    ItemType * volatile data;
} SpscQueue;

SpscQueue SpscQueue_new(unsigned int size);

void SpscQueue_drop(volatile SpscQueue *self);

int SpscQueue_empty(volatile SpscQueue *self);

void SpscQueue_push(volatile SpscQueue *self, ItemType data);

int SpscQueue_full(volatile SpscQueue *self);

ItemType SpscQueue_pop(volatile SpscQueue *self);
