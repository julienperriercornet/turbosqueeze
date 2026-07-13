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
#include <time.h>
#include "platform.h"

#ifdef AVX2
#if _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
#endif

#include "tsq_encode.h"
#include "tsq_common.h"


static inline void outputLits( uint8_t* input, uint8_t* output, uint32_t &i, uint32_t &last_i, uint32_t &j, uint32_t &last_size, uint32_t &n_sym )
{
    do
    {
        uint32_t incr = i-last_i > (8+255) ? (8+255) : i-last_i;

        uint8_t low = incr >= 8 ? 15 : incr + 7;
        uint8_t high = incr - 8;

        if (incr >= 8) output[j++] = high;

        tsq_memcpy8(&output[j], &input[last_i], incr);

        last_i += incr;
        j += incr;
        n_sym ++;

        output[last_size] = (output[last_size] << 4) | low; if ((n_sym & 1) == 0) { last_size = j++; }

        //printf( "i %u j %u lit %u\n", i, j, incr );
    }
    while ((i-last_i) > 0) ;
}


void tsqEncode2_fast( struct TSQCompressionContext* ctx, uint8_t *input, uint8_t *output, uint32_t *outputSize, uint32_t inputSize )
{
    const uint32_t size = inputSize;

    // First write the uncompressed size
    output[0] = (size & 0xFF);
    output[1] = ((size >> 8) & 0xFF);
    output[2] = ((size >> 16) & 0xFF);

    uint32_t i = 0, j = 6;
    uint32_t last_size = j++;
    uint32_t n_sym = 0;
    uint32_t last_i;
    uint32_t current, currhash;
    uint32_t pos;

    do
    {
        last_i = i;

        do
        {
            i++;

            current = *((uint32_t*) &input[i]);
            currhash = (current ^ (current >> 12)) & TSQ_HASH_MASK;
            pos = ctx->refhash[currhash];
            if (pos >= (i & 0xFFFF)) pos += (i & 0xFFFF0000) - 65536;
            else pos += (i & 0xFFFF0000);
            ctx->refhash[currhash] = i;
        }
        while ((i<size) && !((current == *((uint32_t*) &input[pos])) && ((i - pos - 4) < 0xFFFC))) ;

        // output literals
        if ((i-last_i) > 0)
        {
            outputLits( input, output, i, last_i, j, last_size, n_sym );
        }

        if (!(i<size)) break;

        // output matches
        do
        {
            // Calculate the length of this LZ match (It's at least 4 bytes)
            uint64_t* in1 = (uint64_t*) &input[i];
            uint64_t* in2 = (uint64_t*) &input[pos];
            uint64_t xres = (*in1) ^ (*in2);
            uint32_t k = 0;
            while (xres == 0 && k < (4+7+255))
            {
                k += 8;
                in1 ++;
                in2 ++;
                xres = (*in1) ^ (*in2);
            }
            k += stdc_trailing_zeros_ull( xres ) >> 3;

            // Don't overlap with data which hasn't been yet decoded in the decoder.
            k = std::min( k, (uint32_t) (4+7+255) );
            k = (k > (i - pos)) ? (i - pos - 1) : k;

            if ( k < 4 ) break;

            uint8_t low = k > (4+7) ? 7 : (k - 4);
            uint8_t high = k - 11;

            if (k >= (4+7)) output[j++] = high;

            uint32_t offset = i - pos;
            output[j++] = offset & 0xFF;
            output[j++] = offset >> 8;

            i += k;

            // Complete/flush out control and size bytes?
            n_sym++;
            output[last_size] = (output[last_size] << 4) | low; if ((n_sym & 1) == 0) { last_size = j++; }

            // Next match? Likely yes when the hash is build up.
            current = *((uint32_t*) &input[i]);
            currhash = (current ^ (current >> 12)) & TSQ_HASH_MASK;
            pos = ctx->refhash[currhash];
            if (pos >= (i & 0xFFFF)) pos += (i & 0xFFFF0000) - 65536;
            else pos += (i & 0xFFFF0000);
            ctx->refhash[currhash] = i;
        }
        while ((i < size-5) && ((i-pos-4) < 0xFFFC)) ;

    }
    while (true) ;

    output[3] = (n_sym & 0xFF);
    output[4] = ((n_sym >> 8) & 0xFF);
    output[5] = ((n_sym >> 16) & 0xFF);

    // Fill in remaining size byte if odd count
    if ((n_sym & 1) != 0)
    {
        output[last_size] <<= 4;
    }

    *outputSize = j;
}


