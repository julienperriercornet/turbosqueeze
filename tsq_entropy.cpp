/*
 * Turbosqueeze entropy coding implementation.
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


#include "tsq_entropy.h"


static void tsqHuffmanBuildCodes( TSQEntropyContext* ctx, uint16_t node_idx, uint32_t code, uint8_t depth )
{
    const TSQHuffmanNode& node = ctx->nodes[node_idx];

    if (node.symbol >= 0)
    {
        ctx->codes[node.symbol] = code;
        ctx->code_lens[node.symbol] = depth > 0 ? depth : 1; // single-symbol edge case: emit 1-bit code
        return;
    }

    if (node.left)  tsqHuffmanBuildCodes(ctx, node.left,  code,              depth + 1);
    if (node.right) tsqHuffmanBuildCodes(ctx, node.right, code | (1u << depth), depth + 1);
}


void tsqEncodeHuffmann(struct TSQEntropyContext* ctx, uint8_t* input, uint32_t inputSize, uint8_t* output, uint32_t* outputSize)
{
    uint32_t stats[256] = { 0 };

    for (uint32_t i = 0; i < inputSize; i++)
        stats[input[i]]++;

    // Build the Huffman tree using a min-heap (std::priority_queue).
    // Each entry is a node-pool index; the comparator orders by ascending frequency.
    ctx->node_count = 1; // index 0 is the null sentinel
    memset(ctx->codes, 0, sizeof(ctx->codes));
    memset(ctx->code_lens, 0, sizeof(ctx->code_lens));

    auto cmp = [ctx](uint16_t a, uint16_t b) {
        return ctx->nodes[a].freq > ctx->nodes[b].freq; // min-heap
    };
    std::priority_queue<uint16_t, std::vector<uint16_t>, decltype(cmp)> pq(cmp);

    // Create a leaf node for every symbol that appears at least once
    for (uint32_t s = 0; s < 256; s++)
    {
        if (stats[s] == 0) continue;

        uint16_t idx = ctx->node_count++;
        ctx->nodes[idx].freq   = stats[s];
        ctx->nodes[idx].symbol = (int16_t) s;
        ctx->nodes[idx].left   = 0;
        ctx->nodes[idx].right  = 0;
        pq.push(idx);
    }

    if (pq.empty())
    {
        *outputSize = 0;
        return;
    }

    // Repeatedly merge the two lowest-frequency nodes until one root remains
    while (pq.size() > 1)
    {
        uint16_t left  = pq.top(); pq.pop();
        uint16_t right = pq.top(); pq.pop();

        uint16_t parent = ctx->node_count++;
        ctx->nodes[parent].freq   = ctx->nodes[left].freq + ctx->nodes[right].freq;
        ctx->nodes[parent].symbol = -1; // internal node
        ctx->nodes[parent].left   = left;
        ctx->nodes[parent].right  = right;
        pq.push(parent);
    }

    uint16_t root = pq.top();

    // Walk the tree to assign variable-length codes to each symbol
    tsqHuffmanBuildCodes(ctx, root, 0, 0);

    // If any code length exceeds the decode LUT width, the decoder cannot
    // handle it. Signal the caller to fall back to raw storage.
    for (uint32_t s = 0; s < 256; s++)
    {
        if (ctx->code_lens[s] > TSQ_HUFFMAN_MAX_BITS)
        {
            *outputSize = inputSize + 1; // >= rawSize → triggers fallback
            return;
        }
    }

    // --- Write header so the decoder can rebuild the Huffman tree ---
    // Format:
    //   [uint32_t]  originalSize  – number of decoded symbols
    //   [uint8_t[32]] symbolMask  – 256-bit mask: bit s is set if symbol s is present
    //   For each set bit in symbolMask (lexical order):
    //     [uint32_t] frequency
    //   Then the Huffman-coded bitstream follows.

    uint8_t* p = output;

    // Original (uncompressed) symbol count
    memcpy(p, &inputSize, sizeof(uint32_t));
    p += sizeof(uint32_t);

    // 256-bit presence bitmask
    uint8_t symbolMask[32];
    memset(symbolMask, 0, sizeof(symbolMask));
    for (uint32_t s = 0; s < 256; s++)
        if (stats[s]) symbolMask[s >> 3] |= (1u << (s & 7));

    memcpy(p, symbolMask, 32);
    p += 32;

    // Frequencies in lexical order for present symbols
    for (uint32_t s = 0; s < 256; s++)
    {
        if (stats[s] == 0) continue;
        memcpy(p, &stats[s], sizeof(uint32_t));
        p += sizeof(uint32_t);
    }

    uint32_t headerSize = (uint32_t)(p - output);

    // Encode the input into the output bitstream (after the header).
    // Clear only inputSize+2 bytes: if encoding exceeds inputSize bytes it's
    // cheaper to store raw, so we early-exit the loop at that point.
    uint32_t bit_pos = 0;
    uint8_t* bitstream = p;
    memset(bitstream, 0, inputSize + 2);

    for (uint32_t i = 0; i < inputSize; i++)
    {
        uint32_t code = ctx->codes[input[i]];
        uint8_t  len  = ctx->code_lens[input[i]];

        for (uint8_t b = 0; b < len; b++)
        {
            if (code & (1u << b))
                bitstream[bit_pos >> 3] |= (1u << (bit_pos & 7));
            bit_pos++;
        }
    }

    *outputSize = headerSize + ((bit_pos + 7) >> 3);
}


void tsqDecodeHuffmann(struct TSQEntropyContext* ctx, uint8_t* input, uint32_t inputSize, uint8_t* output, uint32_t* outputSize)
{
    const uint8_t* p = input;

    // --- Read header ---
    if (inputSize < sizeof(uint32_t) + 32)
    {
        *outputSize = 0;
        return;
    }

    uint32_t originalSize;
    memcpy(&originalSize, p, sizeof(uint32_t));
    p += sizeof(uint32_t);

    // 256-bit presence bitmask
    uint8_t symbolMask[32];
    memcpy(symbolMask, p, 32);
    p += 32;

    // Read frequencies in lexical order for each set bit
    uint32_t stats[256] = { 0 };
    for (uint32_t s = 0; s < 256; s++)
    {
        if (!(symbolMask[s >> 3] & (1u << (s & 7)))) continue;
        memcpy(&stats[s], p, sizeof(uint32_t));
        p += sizeof(uint32_t);
    }

    // Rebuild the Huffman tree from frequencies (identical algorithm to encoder)
    ctx->node_count = 1;

    auto cmp = [ctx](uint16_t a, uint16_t b) {
        return ctx->nodes[a].freq > ctx->nodes[b].freq;
    };
    std::priority_queue<uint16_t, std::vector<uint16_t>, decltype(cmp)> pq(cmp);

    for (uint32_t s = 0; s < 256; s++)
    {
        if (stats[s] == 0) continue;

        uint16_t idx = ctx->node_count++;
        ctx->nodes[idx].freq   = stats[s];
        ctx->nodes[idx].symbol = (int16_t) s;
        ctx->nodes[idx].left   = 0;
        ctx->nodes[idx].right  = 0;
        pq.push(idx);
    }

    if (pq.empty())
    {
        *outputSize = 0;
        return;
    }

    while (pq.size() > 1)
    {
        uint16_t left  = pq.top(); pq.pop();
        uint16_t right = pq.top(); pq.pop();

        uint16_t parent = ctx->node_count++;
        ctx->nodes[parent].freq   = ctx->nodes[left].freq + ctx->nodes[right].freq;
        ctx->nodes[parent].symbol = -1;
        ctx->nodes[parent].left   = left;
        ctx->nodes[parent].right  = right;
        pq.push(parent);
    }

    uint16_t root = pq.top();

    // Build code tables from the tree (same as encoder)
    memset(ctx->codes, 0, sizeof(ctx->codes));
    memset(ctx->code_lens, 0, sizeof(ctx->code_lens));
    tsqHuffmanBuildCodes(ctx, root, 0, 0);

    // Find the maximum code length
    uint8_t max_bits = 0;
    for (uint32_t s = 0; s < 256; s++)
        if (ctx->code_lens[s] > max_bits)
            max_bits = ctx->code_lens[s];

    if (max_bits > TSQ_HUFFMAN_MAX_BITS)
        max_bits = TSQ_HUFFMAN_MAX_BITS;

    // Populate the decode LUT: for each symbol, fill all table entries
    // whose low code_len bits match the symbol's code.
    uint32_t table_size = 1u << max_bits;
    memset(ctx->decode_len, 0, table_size);

    for (uint32_t s = 0; s < 256; s++)
    {
        uint8_t len = ctx->code_lens[s];
        if (len == 0 || len > max_bits) continue;

        uint32_t code = ctx->codes[s];
        uint32_t fill_count = 1u << (max_bits - len);

        for (uint32_t j = 0; j < fill_count; j++)
        {
            uint32_t idx = code | (j << len);
            ctx->decode_sym[idx] = (uint8_t) s;
            ctx->decode_len[idx] = len;
        }
    }

    // Decode the bitstream using LUT
    const uint8_t* bitstream = p;
    uint32_t bit_pos = 0;
    uint32_t mask = table_size - 1;

    for (uint32_t i = 0; i < originalSize; i++)
    {
        // Peek max_bits from the bitstream (LSB-first, unaligned)
        uint32_t byte_idx = bit_pos >> 3;
        uint32_t bit_off  = bit_pos & 7;

        // Read enough bytes to cover max_bits starting at bit_off
        uint32_t raw = 0;
        memcpy(&raw, bitstream + byte_idx, sizeof(uint32_t));
        uint32_t peek = (raw >> bit_off) & mask;

        output[i]  = ctx->decode_sym[peek];
        bit_pos   += ctx->decode_len[peek];
    }

    *outputSize = originalSize;
}
