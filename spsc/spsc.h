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

void SpscQueue_drop(SpscQueue *self);

int SpscQueue_empty(SpscQueue *self);

void SpscQueue_push(SpscQueue *self, ItemType data);

int SpscQueue_full(SpscQueue *self);

ItemType SpscQueue_pop(SpscQueue *self);
