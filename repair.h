#ifndef REPAIR_H_
#define REPAIR_H_

#include "chunk.h"
#include "util.h"

void cook_chunk_r1(Chunk *chunk);
void cook_chunk_r2(Chunk *chunk);
void repair_2bad_case1(Chunk *chunk, UNUSED_PARAM int i, UNUSED_PARAM int j);
void repair_2bad_case2(Chunk *chunk, int i, UNUSED_PARAM int j);
void repair_2bad_case3(Chunk *chunk, int i, UNUSED_PARAM int j);
void repair_2bad_case4(Chunk *chunk, int i, int j);

#endif
