#include "chunk.h"
#include "packet.h"
#include <assert.h>
#include "util.h"
#include "repair.h"

/**
 * cook_chunk_r1() - 计算第一列校验值，即原始数据每行的异或值。
 */
void cook_chunk_r1(Chunk *chunk) {
    assert(chunk != NULL);
    int m = chunk->p;
    for (int l = 0; l <= m - 2; ++l) {
        PZERO(AT(l, m));
        for (int t = 0; t <= m - 1; ++t) {
            PXOR(AT(l, m), AT(l, t));
        }
    }
}

/**
 * cook_chunk_r2() - 计算第二列校验值，即各种对角线神奇魔法算出来的值。
 */
void cook_chunk_r2(Chunk *chunk) {
    assert(chunk != NULL);
    Packet S;
    PZERO(S);
    int m = chunk->p;
    for (int t = 1; t <= m - 1; ++t)
        PXOR(S, AT(m - 1 - t, t));
    for (int l = 0; l <= m - 2; ++l)
        PASGN(AT(l, m + 1), S);
    for (int t = 0; t <= m - 1; ++t) {
        for (int l = 0; l < (m - 1) - t; ++l) {
            PXOR(AT(l + t, m + 1), AT(l, t));
        }
        for (int l = m - t; l <= m - 2; ++l) {
            PXOR(AT(l + t - m, m + 1), AT(l, t));
        }
    }
}

void repair_2bad_case1(Chunk *chunk, UNUSED_PARAM int i, UNUSED_PARAM int j) {
    /* i == m && j == m + 1 */
    assert(chunk != NULL);
    Packet S;
    PZERO(S);
    int m = chunk->p;

    for (int t = 1; t <= m - 1; ++t)
        PXOR(S, AT(m - 1 - t, t));

    for (int l = 0; l <= m - 2; ++l) {
        PASGN(AT(l, m + 1), S);
        PASGN(AT(l, m), AT(l, m - l - 1));
    }

    for (int t = 0; t <= m - 1; ++t) {
        for (int l = 0; l < (m - 1) - t; ++l) {
            PXOR(AT(l, m), AT(l, t));
            PXOR(AT(l + t, m + 1), AT(l, t));
        }
        for (int l = m - t; l <= m - 2; ++l) {
            PXOR(AT(l, m), AT(l, t));
            PXOR(AT(l + t - m, m + 1), AT(l, t));
        }
    }
}

void repair_2bad_case2(Chunk *chunk, int i, UNUSED_PARAM int j) {
    /* i < m && j == m */
    assert(chunk != NULL);
    assert(i < chunk->p);
    int m = chunk->p;
    Packet S;
    int ref_diagonal = M(i - 1);
    PASGN(S, ATR(ref_diagonal, m + 1));
    for (int l = 0; l <= ref_diagonal; ++l) {
        PXOR(S, ATR(ref_diagonal - l, l));
    }
    for (int l = ref_diagonal + 1; l < m - 1; ++l) {
        PXOR(S, ATR(m + ref_diagonal - l, l));
    }
    if (ref_diagonal < m - 2)
        PXOR(S, ATR(ref_diagonal + 1, m - 1));

    // recover column i
    for (int k = 0; k < m - i && k <= m - 2; ++k) {
        PASGN(AT(k, i), S);
        PXOR(AT(k, i), ATR(i + k, m + 1));

        /* l < i <= k + i < m */
        for (int l = 0; l < i; ++l)
            PXOR(AT(k, i), ATR(k + i - l, l));
        /* i <= k + i < m */
        for (int l = i + 1; l <= m - 1; ++l)
            PXOR(AT(k, i), ATR(M(k + i - l), l));
    }
    for (int k = m - i; k <= m - 2; ++k) {
        PASGN(AT(k, i), S);
        PXOR(AT(k, i), ATR(i + k - m, m + 1));
        for (int l = 0; l < i; ++l)
            PXOR(AT(k, i), ATR(M(k + i - l), l));
        for (int l = i + 1; l <= m - 1; ++l)
            PXOR(AT(k, i), ATR(M(k + i - l), l));
    }
    cook_chunk_r1(chunk);
}

void repair_2bad_case3(Chunk *chunk, int i, UNUSED_PARAM int j) {
    /* i < m && j == m + 1 */
    assert(chunk != NULL);
    assert(i < chunk->p);
    int m = chunk->p;
    for (int k = 0; k < m - 1; ++k)
        PZERO(AT(k, i));
    for (int l = 0; l < i; ++l) {
        for (int k = 0; k < m - 1; ++k) {
            PXOR(AT(k, i), AT(k, l));
        }
    }
    for (int l = i + 1; l <= m; ++l) {
        for (int k = 0; k < m - 1; ++k) {
            PXOR(AT(k, i), AT(k, l));
        }
    }
    cook_chunk_r2(chunk);
}

void repair_2bad_case4(Chunk *chunk, int i, int j) {
    assert(chunk != NULL);
    assert(i < chunk->p);
    assert(j < chunk->p);
    int m = chunk->p;
    /* 损坏的是两块原始数据磁盘 */
    Packet S;
    PZERO(S);
    for (int l = 0; l <= m - 2; ++l) {
        PXOR(S, ATR(l, m));
        PXOR(S, ATR(l, m + 1));
    }
    // horizontal syndromes S0
    // diagonal syndromes S1
    Packet S0[PMAX];
    Packet S1[PMAX];

    for (int u = 0; u <= m - 1; ++u) {
        PZERO(S0[u]);
        PASGN(S1[u], S);
        PXOR(S1[u], ATR(u, m + 1));
    }

    for (int l = 0; l <= m; ++l) {
        if (l == i || l == j)
            continue;
        for (int u = 0; u < m - 1; ++u)
            PXOR(S0[u], AT(u, l));
    }

    if (i != 0 && j != 0) {
        for (int u = 0; u < m - 1; ++u)
            PXOR(S1[u], AT(u, 0));
    }
    for (int l = 1; l <= m - 1; ++l) {
        if (l == i || l == j)
            continue;
        for (int u = 0; u < l - 1; ++u)
            PXOR(S1[u], AT(m + u - l, l));
        for (int u = l; u < m; ++u)
            PXOR(S1[u], AT(u - l, l));
    }

    int step = j - i;
    for (int s = m - 1 - step; s != m - 1; s -= step) {
        AT(s, j) = S1[M(s + j)];
        PXOR(AT(s, j), ATR(M(s + step), i));
        AT(s, i) = S0[s];
        PXOR(AT(s, i), AT(s, j));
        s += m * (s < step);
    }
}

