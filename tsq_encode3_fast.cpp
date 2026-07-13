/*
 * Turbosqueeze encoder.
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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <vector>

#include "turbosqueeze.h"
#include "tsq_common.h"
#include "tsq_entropy.h"
#include "platform.h"


static void tsqWriteRawStream(std::vector<uint8_t>& buf, uint8_t*& p)
{
    uint32_t rawSize = (uint32_t) buf.size();

    p[0] = 0;
    p[1] = (rawSize & 0xFF);
    p[2] = ((rawSize >> 8) & 0xFF);
    p[3] = ((rawSize >> 16) & 0xFF);
    p += sizeof(uint32_t);

    if (rawSize > 0)
    {
        memcpy(p, buf.data(), rawSize);
        p += rawSize;
    }
}

static void tsqWriteHuffmanStream(TSQEntropyContext* ectx, std::vector<uint8_t>& buf, uint8_t*& p)
{
    uint32_t rawSize = (uint32_t) buf.size();

    if (rawSize == 0)
    {
        memset(p, 0, sizeof(uint32_t));
        p += sizeof(uint32_t);
        return;
    }

    p[0] = 1;
    p++;

    uint8_t* sizeSlot = p;
    p += 3;

    uint32_t encodedSize = 0;
    tsqEncodeHuffmann(ectx, buf.data(), rawSize, p, &encodedSize);

    if (!(encodedSize < rawSize))
    {
        p -= sizeof(uint32_t);
        tsqWriteRawStream(buf, p);
        return;
    }

    sizeSlot[0] = (encodedSize & 0xFF);
    sizeSlot[1] = ((encodedSize >> 8) & 0xFF);
    sizeSlot[2] = ((encodedSize >> 16) & 0xFF);

    p += encodedSize;
}

static inline void pushNibble(std::vector<uint8_t>& sizeBuffer, uint8_t nibble, uint32_t& n_sym)
{
    if ((n_sym & 1) == 0)
        sizeBuffer.push_back(nibble << 4);
    else
        sizeBuffer.back() |= nibble;
    n_sym++;
}

static void outputLits3(uint8_t* input, uint32_t from, uint32_t to,
    std::vector<uint8_t>& sizeBuffer, std::vector<uint8_t>& literalsBuffer,
    std::vector<uint8_t>& extraLitSizeBuffer, uint32_t& n_sym)
{
    uint32_t remaining = to - from;
    while (remaining > 0)
    {
        uint32_t incr = remaining > (8 + 255) ? (8 + 255) : remaining;

        if (incr >= 8)
        {
            pushNibble(sizeBuffer, 15, n_sym); // control=1, extra=1
            extraLitSizeBuffer.push_back((uint8_t)(incr - 8));
        }
        else
        {
            pushNibble(sizeBuffer, (uint8_t)(incr + 7), n_sym); // control=1, sz=incr
        }

        literalsBuffer.insert(literalsBuffer.end(), &input[from], &input[from + incr]);
        from += incr;
        remaining -= incr;
    }
}

static void outputMatch3(uint32_t offset, uint32_t k,
    std::vector<uint8_t>& sizeBuffer, std::vector<uint8_t>& offsetHighBuffer,
    std::vector<uint8_t>& offsetLowBuffer, std::vector<uint8_t>& extraRepSizeBuffer,
    uint32_t& n_sym)
{
    if (k >= (4 + 7))
    {
        pushNibble(sizeBuffer, 7, n_sym); // control=0, extra=1
        extraRepSizeBuffer.push_back((uint8_t)(k - 11));
    }
    else
    {
        pushNibble(sizeBuffer, (uint8_t)(k - 4), n_sym); // control=0, sz=k
    }

    offsetHighBuffer.push_back((uint8_t)(offset >> 8));
    offsetLowBuffer.push_back((uint8_t)(offset & 0xFF));
}


void tsqEncode3_fast( struct TSQCompressionContext3* ctx, uint8_t *input, uint8_t *output, uint32_t *outputSize, uint32_t inputSize )
{
    const uint32_t size = inputSize;

    std::vector<uint8_t> sizeBuffer;
    std::vector<uint8_t> offsetHighBuffer;
    std::vector<uint8_t> offsetLowBuffer;
    std::vector<uint8_t> literalsBuffer;
    std::vector<uint8_t> extraLitSizeBuffer;
    std::vector<uint8_t> extraRepSizeBuffer;

    uint32_t n_sym = 0;
    uint32_t i = 0, last_i;
    uint32_t current, currhash, pos;

    do
    {
        last_i = i;

        do
        {
            i++;
            current = *((uint32_t*) &input[i]);
            currhash = (current ^ (current >> 12)) & TSQ_HASH_MASK;
            pos = ctx->refhash[currhash];
            ctx->refhash[currhash] = i;
        }
        while ((i < size) && !((current == *((uint32_t*) &input[pos])) && ((i - pos - 4) < 0xFFFC)));

        if ((i - last_i) > 0)
            outputLits3(input, last_i, i, sizeBuffer, literalsBuffer, extraLitSizeBuffer, n_sym);

        if (!(i < size)) break;

        do
        {
            uint64_t* in1 = (uint64_t*) &input[i];
            uint64_t* in2 = (uint64_t*) &input[pos];
            uint64_t xres = (*in1) ^ (*in2);
            uint32_t k = 0;
            while (xres == 0 && k < (4 + 7 + 255))
            {
                k += 8;
                in1++;
                in2++;
                xres = (*in1) ^ (*in2);
            }
            k += stdc_trailing_zeros_ull(xres) >> 3;

            k = std::min(k, (uint32_t)(4 + 7 + 255));
            k = (k > (i - pos)) ? (i - pos - 1) : k;

            if (k < 4) break;

            uint32_t offset = i - pos;
            outputMatch3(offset, k, sizeBuffer, offsetHighBuffer, offsetLowBuffer, extraRepSizeBuffer, n_sym);

            i += k;

            current = *((uint32_t*) &input[i]);
            currhash = (current ^ (current >> 12)) & TSQ_HASH_MASK;
            pos = ctx->refhash[currhash];
            ctx->refhash[currhash] = i;
        }
        while ((i < size - 5) && ((i - pos - 4) < 0xFFFC));
    }
    while (true);

    // Write v3 entropy-coded header + streams
    output[0] = (size & 0xFF);
    output[1] = ((size >> 8) & 0xFF);
    output[2] = ((size >> 16) & 0xFF);
    output[3] = 1; // entropyCoding
    output[4] = (n_sym & 0xFF);
    output[5] = ((n_sym >> 8) & 0xFF);
    output[6] = ((n_sym >> 16) & 0xFF);

    uint8_t* p = &output[7];
    TSQEntropyContext ectx;

    tsqWriteHuffmanStream(&ectx, sizeBuffer, p);
    tsqWriteHuffmanStream(&ectx, offsetHighBuffer, p);
    tsqWriteHuffmanStream(&ectx, literalsBuffer, p);
    tsqWriteHuffmanStream(&ectx, extraLitSizeBuffer, p);
    tsqWriteHuffmanStream(&ectx, extraRepSizeBuffer, p);
    tsqWriteRawStream(offsetLowBuffer, p);

    *outputSize = (uint32_t)(p - output);
}

