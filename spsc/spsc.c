#include "spsc.h"

static unsigned int roundup_pow_two(unsigned int size) {
    while (size - (size & (-size)) != 0) {
        size += (size & (-size));
    }
    return size;
}

SpscQueue SpscQueue_new(unsigned int size) {
    return (SpscQueue) {
        .data = malloc(sizeof(ItemType) * size),
        .in = 0,
        .out = 0,
        .mask = roundup_pow_two(size) - 1
    };
}

void SpscQueue_drop(SpscQueue *self) {
    free(self->data);
    *self = (SpscQueue) {
        .data = NULL,
        .in = 0,
        .out = 0,
        .mask = 0
    };
}

int SpscQueue_empty(const SpscQueue *self) {
    return self->out == self->in;
}

void SpscQueue_push(SpscQueue *self, ItemType data) {
    while (self->in - self->out > self->mask)
        ;
    unsigned int off = self->in & self->mask;
    self->data[off] = data;
    write_barrier();
    self->in++;
}

ItemType SpscQueue_pop(SpscQueue *self) {
    while (SpscQueue_empty(self))
        ;
    unsigned int off = self->out & self->mask;
    ItemType res = self->data[off];
    read_barrier();
    self->out++;
    return res;
}
