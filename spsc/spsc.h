#include <stdlib.h>

#define read_barrier()  asm volatile("":::"memory")
#define write_barrier() asm volatile("":::"memory")

typedef int ItemType;

typedef struct {
    unsigned int in;
    unsigned int out;
    unsigned int mask;
    ItemType *data;
} SpscQueue;

SpscQueue SpscQueue_new(unsigned int size);

void SpscQueue_drop(SpscQueue *self);

int SpscQueue_empty(const SpscQueue *self);

void SpscQueue_push(SpscQueue *self, ItemType data);

ItemType SpscQueue_pop(SpscQueue *self);
