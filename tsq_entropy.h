#pragma once
/*
 * Turbosqueeze entropy coding header.
 *
 * Copyright (c) 2024-2026 Julien Perrier-cornet
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


#include <cstdint>
#include <cstring>
#include <queue>
#include <vector>
#include <functional>


struct TSQHuffmanNode
{
    uint32_t freq;
    int16_t symbol;    // 0-255 for leaf nodes, -1 for internal nodes
    uint16_t left;     // index into node pool (0 = no child)
    uint16_t right;    // index into node pool (0 = no child)
};


#define TSQ_HUFFMAN_MAX_BITS 17

struct TSQEntropyContext
{
    // Node pool: at most 256 leaves + 255 internal nodes = 511 nodes.
    // Index 0 is reserved as the null sentinel.
    TSQHuffmanNode nodes[512];
    uint32_t node_count;

    // Per-symbol code table produced by tree traversal
    uint32_t codes[256];      // bit pattern (LSB-first)
    uint8_t  code_lens[256];  // length in bits (0 = symbol not present)

    // Decode lookup table: indexed by peeked bits, yields symbol + code length
    uint8_t  decode_sym[1 << TSQ_HUFFMAN_MAX_BITS];
    uint8_t  decode_len[1 << TSQ_HUFFMAN_MAX_BITS];
};


void tsqEncodeHuffmann(struct TSQEntropyContext* ctx, uint8_t* input, uint32_t inputSize, uint8_t* output, uint32_t* outputSize);
void tsqDecodeHuffmann(struct TSQEntropyContext* ctx, uint8_t* input, uint32_t inputSize, uint8_t* output, uint32_t* outputSize);


