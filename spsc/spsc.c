#include "spsc.h"
#include <stdio.h>

static unsigned int roundup_pow_two(unsigned int size) {
    while (size - (size & (-size)) != 0) {
        size += (size & (-size));
    }
    return size;
}

SpscQueue SpscQueue_new(unsigned int size) {
    return (SpscQueue) {
#ifdef PERFCNT
        .push = {
            .wait = 0,
            .cnt = 0,
        },
        .pop = {
            .wait = 0,
            .cnt = 0,
        },
#endif
        .data = malloc(sizeof(ItemType) * roundup_pow_two(size)),
        .in = 0,
        .out = 0,
        .mask = roundup_pow_two(size) - 1
    };
}

void SpscQueue_drop(SpscQueue *self) {
    free(self->data);
    *self = (SpscQueue) {
#ifdef PERFCNT
        .push = {
            .wait = 0,
            .cnt = 0,
        },
        .pop = {
            .wait = 0,
            .cnt = 0,
        },
#endif
        .data = NULL,
        .in = 0,
        .out = 0,
        .mask = 0
    };
}

int SpscQueue_empty(SpscQueue *self) {
    return self->out == self->in;
}

int SpscQueue_full(SpscQueue *self) {
    return self->in - self->out == self->mask + 1;
}

void SpscQueue_push(SpscQueue *self, ItemType data) {
#ifdef PERFCNT
    // FIXME: barrier: magic
    write_barrier();
    if (SpscQueue_full(self)) {
        self->push.wait += 1;
    }
    self->push.cnt += 1;
    write_barrier();
#endif

    while (SpscQueue_full(self))
        ;
    unsigned int off = self->in & self->mask;
    self->data[off] = data;
    write_barrier();
    self->in++;
}

ItemType SpscQueue_pop(SpscQueue *self) {
#ifdef PERFCNT
    // FIXME: barrier: magic
    write_barrier();
    if (SpscQueue_empty(self)) {
        self->pop.wait += 1;
    }
    self->pop.cnt += 1;
    write_barrier();
#endif

    while (SpscQueue_empty(self))
        ;
    unsigned int off = self->out & self->mask;
    ItemType res = self->data[off];
    read_barrier();
    self->out++;
    return res;
}

#ifdef PERFCNT
void SpscQueue_perf(SpscQueue *self, const char *prompt) {
    fprintf(stderr, "%s: pop block %zu%%(%zu/%zu), push block %zu%%(%zu/%zu)\n",
            prompt,
            self->pop.wait * 100 / self->pop.cnt, self->pop.wait, self->pop.cnt,
            self->push.wait * 100 / self->push.cnt, self->push.wait, self->push.cnt);
}
#endif
