/*
 * Turbosqueeze encoder v3.
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


#include <cassert>
#include <cstdio>

#include "turbosqueeze.h"
#include "tsq_common.h"
#include "tsq_entropy.h"


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
        // Write zero size
        memset(p, 0, sizeof(uint32_t));
        p += sizeof(uint32_t);
        return;
    }

    // Indicate Huffman encoded stream
    p[0] = 1;
    p++;

    // Reserve space for the encoded size, then encode after it
    uint8_t* sizeSlot = p;
    p += 3;

    uint32_t encodedSize = 0;
    tsqEncodeHuffmann(ectx, buf.data(), rawSize, p, &encodedSize);

    if (!(encodedSize < rawSize))
    {
        p -= sizeof(uint32_t);
        tsqWriteRawStream( buf, p );
        return;
    }

    // Fill in encoded size
    sizeSlot[0] = (encodedSize & 0xFF);
    sizeSlot[1] = ((encodedSize >> 8) & 0xFF);
    sizeSlot[2] = ((encodedSize >> 16) & 0xFF);

    p += encodedSize;
}


void tsqEncode3_fast( struct TSQCompressionContext* ctx, uint8_t *input, uint8_t *output, uint32_t *outputSize, uint32_t inputSize )
{
    tsqEncode2_fast(ctx, input, output, outputSize, inputSize);

    std::vector<uint8_t> sizeBuffer;
    std::vector<uint8_t> offsetHighBuffer;
    std::vector<uint8_t> offsetLowBuffer;
    std::vector<uint8_t> literalsBuffer;
    std::vector<uint8_t> extraLitSizeBuffer;
    std::vector<uint8_t> extraRepSizeBuffer;

    uint32_t n_blocks = output[3] | (output[4] << 8) | (output[5] << 16);
    uint32_t i = 6, j = 0, n = 0;

    while (n < n_blocks)
    {
        uint8_t size_byte = output[i++];
        sizeBuffer.push_back( size_byte );
        uint32_t sz = (size_byte>>4);
        uint32_t control = sz & 8;
        uint32_t extra = (sz & 7) == 7;

        sz = control ? (sz & 7) + 1 : (sz & 7) + 4;
        if (extra)
        {
            if (control)
                extraLitSizeBuffer.push_back( output[i] );
            else
                extraRepSizeBuffer.push_back( output[i] );
            sz += output[i++];
        }

        if (control)
        {
            literalsBuffer.insert(literalsBuffer.end(), &output[i], &output[i+sz]);
        }
        else
        {
            uint16_t offset = tsq_read16( &output[i] );
            offsetHighBuffer.push_back( offset >> 8 );
            offsetLowBuffer.push_back( offset & 0xFF );
        }

        j += sz;
        i += control ? sz : 2;
        n ++;

        if (!(n < n_blocks)) break;

        sz = (size_byte & 15);
        control = sz & 8;
        extra = (sz & 7) == 7;

        sz = control ? (sz & 7) + 1 : (sz & 7) + 4;
        if (extra)
        {
            if (control)
                extraLitSizeBuffer.push_back( output[i] );
            else
                extraRepSizeBuffer.push_back( output[i] );
            sz += output[i++];
        }

        if (control)
        {
            literalsBuffer.insert(literalsBuffer.end(), &output[i], &output[i+sz]);
        }
        else
        {
            uint16_t offset = tsq_read16( &output[i] );
            offsetHighBuffer.push_back( offset >> 8 );
            offsetLowBuffer.push_back( offset & 0xFF );
        }

        j += sz;
        i += control ? sz : 2;
        n ++;
    }

    // Check if we didn't skip any valuable data.
    assert( sizeBuffer.size() + offsetHighBuffer.size() + offsetLowBuffer.size() + literalsBuffer.size() + extraLitSizeBuffer.size() + extraRepSizeBuffer.size() == (i - 6) );

    // Compress each buffer with the specified entropy coding method and write the final output format.
    const uint32_t size = inputSize;

    // First write the uncompressed size
    output[0] = (size & 0xFF);
    output[1] = ((size >> 8) & 0xFF);
    output[2] = ((size >> 16) & 0xFF);
    output[3] = 1;

    // Write n_blocks
    output[4] = (n_blocks & 0xFF);
    output[5] = ((n_blocks >> 8) & 0xFF);
    output[6] = ((n_blocks >> 16) & 0xFF);

    uint8_t* p = &output[7];
    TSQEntropyContext ectx;

    // Huffman-encode structured streams
    tsqWriteHuffmanStream(&ectx, sizeBuffer, p);
    tsqWriteHuffmanStream(&ectx, offsetHighBuffer, p);
    tsqWriteHuffmanStream(&ectx, literalsBuffer, p);
    tsqWriteHuffmanStream(&ectx, extraLitSizeBuffer, p);
    tsqWriteHuffmanStream(&ectx, extraRepSizeBuffer, p);

    // offsetLowBuffer is essentially random — store raw
    tsqWriteRawStream(offsetLowBuffer, p);

    *outputSize = (uint32_t)(p - output);
}


void tsqEncode3_hist( struct TSQCompressionContextHist* ctx, uint8_t *input, uint8_t *output, uint32_t *outputSize, uint32_t inputSize, uint8_t entropyCoding )
{
    tsqEncode2_hist(ctx, input, output, outputSize, inputSize);

    std::vector<uint8_t> sizeBuffer;
    std::vector<uint8_t> offsetHighBuffer;
    std::vector<uint8_t> offsetLowBuffer;
    std::vector<uint8_t> literalsBuffer;
    std::vector<uint8_t> extraLitSizeBuffer;
    std::vector<uint8_t> extraRepSizeBuffer;

    uint32_t n_blocks = output[3] | (output[4] << 8) | (output[5] << 16);
    uint32_t i = 6, j = 0, n = 0;

    while (n < n_blocks)
    {
        uint8_t size_byte = output[i++];
        sizeBuffer.push_back( size_byte );
        uint32_t sz = (size_byte>>4);
        uint32_t control = sz & 8;
        uint32_t extra = (sz & 7) == 7;

        sz = control ? (sz & 7) + 1 : (sz & 7) + 4;
        if (extra)
        {
            if (control)
                extraLitSizeBuffer.push_back( output[i] );
            else
                extraRepSizeBuffer.push_back( output[i] );
            sz += output[i++];
        }

        if (control)
        {
            literalsBuffer.insert(literalsBuffer.end(), &output[i], &output[i+sz]);
        }
        else
        {
            uint16_t offset = tsq_read16( &output[i] );
            offsetHighBuffer.push_back( offset >> 8 );
            offsetLowBuffer.push_back( offset & 0xFF );
        }

        j += sz;
        i += control ? sz : 2;
        n ++;

        if (!(n < n_blocks)) break;

        sz = (size_byte & 15);
        control = sz & 8;
        extra = (sz & 7) == 7;

        sz = control ? (sz & 7) + 1 : (sz & 7) + 4;
        if (extra)
        {
            if (control)
                extraLitSizeBuffer.push_back( output[i] );
            else
                extraRepSizeBuffer.push_back( output[i] );
            sz += output[i++];
        }

        if (control)
        {
            literalsBuffer.insert(literalsBuffer.end(), &output[i], &output[i+sz]);
        }
        else
        {
            uint16_t offset = tsq_read16( &output[i] );
            offsetHighBuffer.push_back( offset >> 8 );
            offsetLowBuffer.push_back( offset & 0xFF );
        }

        j += sz;
        i += control ? sz : 2;
        n ++;
    }

    // Check if we didn't skip any valuable data.
    assert( sizeBuffer.size() + offsetHighBuffer.size() + offsetLowBuffer.size() + literalsBuffer.size() + extraLitSizeBuffer.size() + extraRepSizeBuffer.size() == (i - 6) );

    // Compress each buffer with the specified entropy coding method and write the final output format.
    const uint32_t size = inputSize;

    // First write the uncompressed size
    output[0] = (size & 0xFF);
    output[1] = ((size >> 8) & 0xFF);
    output[2] = ((size >> 16) & 0xFF);
    output[3] = entropyCoding;

    // Write n_blocks
    output[4] = (n_blocks & 0xFF);
    output[5] = ((n_blocks >> 8) & 0xFF);
    output[6] = ((n_blocks >> 16) & 0xFF);

    uint8_t* p = &output[7];
    TSQEntropyContext ectx;

    // Huffman-encode structured streams
    tsqWriteHuffmanStream(&ectx, sizeBuffer, p);
    tsqWriteHuffmanStream(&ectx, offsetHighBuffer, p);
    tsqWriteHuffmanStream(&ectx, literalsBuffer, p);
    tsqWriteHuffmanStream(&ectx, extraLitSizeBuffer, p);
    tsqWriteHuffmanStream(&ectx, extraRepSizeBuffer, p);

    // offsetLowBuffer is essentially random — store raw
    tsqWriteRawStream(offsetLowBuffer, p);

    *outputSize = (uint32_t)(p - output);
}


void tsqEncode3_opt( TSQOptContext* ctx, uint8_t *input, uint8_t *output, uint32_t *outputSize, uint32_t inputSize, uint8_t entropyCoding )
{
    tsqEncode2_opt(ctx, input, output, outputSize, inputSize);

    std::vector<uint8_t> sizeBuffer;
    std::vector<uint8_t> offsetHighBuffer;
    std::vector<uint8_t> offsetLowBuffer;
    std::vector<uint8_t> literalsBuffer;
    std::vector<uint8_t> extraLitSizeBuffer;
    std::vector<uint8_t> extraRepSizeBuffer;

    uint32_t n_blocks = output[3] | (output[4] << 8) | (output[5] << 16);
    uint32_t i = 6, j = 0, n = 0;

    while (n < n_blocks)
    {
        uint8_t size_byte = output[i++];
        sizeBuffer.push_back( size_byte );
        uint32_t sz = (size_byte>>4);
        uint32_t control = sz & 8;
        uint32_t extra = (sz & 7) == 7;

        sz = control ? (sz & 7) + 1 : (sz & 7) + 4;
        if (extra)
        {
            if (control)
                extraLitSizeBuffer.push_back( output[i] );
            else
                extraRepSizeBuffer.push_back( output[i] );
            sz += output[i++];
        }

        if (control)
        {
            literalsBuffer.insert(literalsBuffer.end(), &output[i], &output[i+sz]);
        }
        else
        {
            uint16_t offset = tsq_read16( &output[i] );
            offsetHighBuffer.push_back( offset >> 8 );
            offsetLowBuffer.push_back( offset & 0xFF );
        }

        j += sz;
        i += control ? sz : 2;
        n ++;

        if (!(n < n_blocks)) break;

        sz = (size_byte & 15);
        control = sz & 8;
        extra = (sz & 7) == 7;

        sz = control ? (sz & 7) + 1 : (sz & 7) + 4;
        if (extra)
        {
            if (control)
                extraLitSizeBuffer.push_back( output[i] );
            else
                extraRepSizeBuffer.push_back( output[i] );
            sz += output[i++];
        }

        if (control)
        {
            literalsBuffer.insert(literalsBuffer.end(), &output[i], &output[i+sz]);
        }
        else
        {
            uint16_t offset = tsq_read16( &output[i] );
            offsetHighBuffer.push_back( offset >> 8 );
            offsetLowBuffer.push_back( offset & 0xFF );
        }

        j += sz;
        i += control ? sz : 2;
        n ++;
    }

    // Check if we didn't skip any valuable data.
    assert( sizeBuffer.size() + offsetHighBuffer.size() + offsetLowBuffer.size() + literalsBuffer.size() + extraLitSizeBuffer.size() + extraRepSizeBuffer.size() == (i - 6) );

    // Compress each buffer with the specified entropy coding method and write the final output format.
    const uint32_t size = inputSize;

    // First write the uncompressed size
    output[0] = (size & 0xFF);
    output[1] = ((size >> 8) & 0xFF);
    output[2] = ((size >> 16) & 0xFF);
    output[3] = entropyCoding;

    // Write n_blocks
    output[4] = (n_blocks & 0xFF);
    output[5] = ((n_blocks >> 8) & 0xFF);
    output[6] = ((n_blocks >> 16) & 0xFF);

    uint8_t* p = &output[7];
    TSQEntropyContext ectx;

    // Huffman-encode structured streams
    tsqWriteHuffmanStream(&ectx, sizeBuffer, p);
    tsqWriteHuffmanStream(&ectx, offsetHighBuffer, p);
    tsqWriteHuffmanStream(&ectx, literalsBuffer, p);
    tsqWriteHuffmanStream(&ectx, extraLitSizeBuffer, p);
    tsqWriteHuffmanStream(&ectx, extraRepSizeBuffer, p);

    // offsetLowBuffer is essentially random — store raw
    //tsqWriteRawStream(offsetLowBuffer, p);
    tsqWriteHuffmanStream(&ectx, offsetLowBuffer, p);

    *outputSize = (uint32_t)(p - output);
}

