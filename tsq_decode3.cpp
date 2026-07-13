/*
 * Turbosqueeze decoder implementation.
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
#include <vector>

#ifdef AVX2
#if _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
#endif

#include "tsq_decode.h"
#include "tsq_common.h"
#include "tsq_entropy.h"


// Read one entropy stream (Huffman or raw) from p, advancing p. Returns nullptr on error.
static const uint8_t* tsqReadStream3( const uint8_t* p, const uint8_t* end, uint8_t *buffer, uint32_t& bufferStart, uint32_t& bufferEnd )
{
    if (p + 4 > end) return nullptr;

    uint8_t flag = p[0];
    uint32_t streamSize = p[1] | (p[2] << 8) | (p[3] << 16);
    p += 4;

    if (streamSize == 0) return p;
    if (p + streamSize > end) return nullptr;

    if (flag == 0)
    {
        memcpy(buffer + bufferStart, p, streamSize);
        bufferEnd = bufferStart + streamSize;
    }
    else
    {
        TSQEntropyContext ectx;
        uint32_t decodedSize = 0;
        tsqDecodeHuffmann(&ectx, (uint8_t*) p, streamSize, buffer + bufferStart, &decodedSize);
        bufferEnd = bufferStart + decodedSize;
    }

    return p + streamSize;
}

void tsqDecode3( struct TSQDecompressionContext3* ctx, uint8_t *inputBlock, uint8_t *outputBlock, uint32_t *outputSize, uint32_t inputSize )
{
    *outputSize = 0;

    // Header: uncompressed size (3 bytes) | entropyCoding (1 byte) | n_blocks (3 bytes)
    if (inputSize < 7) return;

    uint32_t size = inputBlock[0] | (inputBlock[1] << 8) | (inputBlock[2] << 16);
    if (!(size <= TSQ_BLOCK_SZ)) return;

    // inputBlock[3] = entropyCoding — streams are self-describing, not needed by decoder
    uint32_t n_blocks = inputBlock[4] | (inputBlock[5] << 8) | (inputBlock[6] << 16);

    const uint8_t* p = inputBlock + 7;
    const uint8_t* end = inputBlock + inputSize;

    uint32_t si = 0, ohi = 0, oli = 0, li = 0, eli = 0, eri = 0;

    if (!(p = tsqReadStream3(p, end, ctx->sizeBuffer, si, ctx->sizeBufferEnd)))        return;
    if (!(p = tsqReadStream3(p, end, ctx->offsetHighBuffer, ohi, ctx->offsetHighBufferEnd)))   return;
    if (!(p = tsqReadStream3(p, end, ctx->literalsBuffer, li, ctx->literalsBufferEnd)))     return;
    if (!(p = tsqReadStream3(p, end, ctx->extraLitSizeBuffer, eli, ctx->extraLitSizeBufferEnd))) return;
    if (!(p = tsqReadStream3(p, end, ctx->extraRepSizeBuffer, eri, ctx->extraRepSizeBufferEnd))) return;
    if (!(p = tsqReadStream3(p, end, ctx->offsetLowBuffer, oli, ctx->offsetLowBufferEnd)))    return;

    const uint8_t* sizeBuffer         = ctx->sizeBuffer;
    const uint8_t* offsetHighBuffer   = ctx->offsetHighBuffer;
    const uint8_t* literalsBuffer     = ctx->literalsBuffer;
    const uint8_t* extraLitSizeBuffer = ctx->extraLitSizeBuffer;
    const uint8_t* extraRepSizeBuffer = ctx->extraRepSizeBuffer;
    const uint8_t* offsetLowBuffer    = ctx->offsetLowBuffer;
    si = ohi = oli = li = eli = eri = 0;

    uint32_t j = 0, n = 0;
    uint32_t n_fastblocks = (n_blocks - 4) & ~1;

    while (n < n_fastblocks)
    {
        uint8_t size_byte = sizeBuffer[si++];

        #pragma unroll 2
        for (int sub = 0; sub < 2; sub++, n++)
        {
            uint32_t nibble  = (sub == 0) ? (size_byte >> 4) : (size_byte & 15);
            uint32_t control = nibble & 8;
            uint32_t extra   = (nibble & 7) == 7;
            uint32_t sz      = control ? (nibble & 7) + 1 : (nibble & 7) + 4;

            if (extra)
            {
                if (control)
                {
                    sz += extraLitSizeBuffer[eli++];
                }
                else
                {
                    sz += extraRepSizeBuffer[eri++];
                }
            }

            if (j + sz > size) return;

            if (control)
            {
                tsq_memcpy8(&outputBlock[j], const_cast<uint8_t*>(&literalsBuffer[li]), sz);
                li += sz;
            }
            else
            {
                uint32_t offset = (uint32_t(offsetHighBuffer[ohi++]) << 8) | offsetLowBuffer[oli++];
                tsq_memcpy8(&outputBlock[j], &outputBlock[j - offset], sz);
            }

            j += sz;
        }
    }

    while (n < n_blocks)
    {
        uint8_t size_byte = sizeBuffer[si++];

        #pragma unroll 2
        for (int sub = 0; sub < 2 && n < n_blocks; sub++, n++)
        {
            uint32_t nibble  = (sub == 0) ? (size_byte >> 4) : (size_byte & 15);
            uint32_t control = nibble & 8;
            uint32_t extra   = (nibble & 7) == 7;
            uint32_t sz      = control ? (nibble & 7) + 1 : (nibble & 7) + 4;

            if (extra)
            {
                if (control)
                {
                    sz += extraLitSizeBuffer[eli++];
                }
                else
                {
                    sz += extraRepSizeBuffer[eri++];
                }
            }

            if (j + sz > size) return;

            if (control)
            {
                memcpy(&outputBlock[j], &literalsBuffer[li], sz);
                li += sz;
            }
            else
            {
                uint32_t offset = (uint32_t(offsetHighBuffer[ohi++]) << 8) | offsetLowBuffer[oli++];
                memcpy(&outputBlock[j], &outputBlock[j - offset], sz);
            }

            j += sz;
        }
    }

    *outputSize = size;
}


