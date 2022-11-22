#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include "spsc.h"

SpscQueue fifo;
#define RANDNUMSIZE 10000000
int randnums[RANDNUMSIZE];
int outnums[RANDNUMSIZE];
int arr_read_pos;
int arr_write_pos;

void *producer(void *ptr) {
    while (arr_read_pos < RANDNUMSIZE) {
        SpscQueue_push(&fifo, randnums[arr_read_pos++]);
    }
    return NULL;
}

void *customer(void *ptr) {
    while (arr_write_pos < RANDNUMSIZE) {
        outnums[arr_write_pos++] = SpscQueue_pop(&fifo);
    }
    return NULL;
}

int main(void) {
    time_t t;
    srand((unsigned) time(&t));
    for (int i = 0; i < RANDNUMSIZE; ++i) {
        randnums[i] = rand() % 100;
    }
    fifo = SpscQueue_new(100);
    pthread_t t1, t2;
    pthread_create(&t1, NULL, producer, NULL);
    pthread_create(&t2, NULL, customer, NULL);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    SpscQueue_drop(&fifo);
    for (int i = 0; i < RANDNUMSIZE; ++i) {
        if (randnums[i] != outnums[i]) {
            return 1;
        }
    }
    return 0;
}
