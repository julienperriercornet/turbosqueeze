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


static inline uint32_t load_u32_unaligned(const uint8_t* ptr)
{
    uint32_t value;
    memcpy(&value, ptr, sizeof(value));
    return value;
}


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
    }
    while ((i-last_i) > 0) ;
}


static inline void searchBestLZMatch( uint8_t *input, uint32_t *cnthash, uint16_t *refhash, uint32_t i, uint32_t current, uint32_t currhash, uint32_t n_rep, uint32_t &maxk, uint32_t &maxpos )
{
    uint32_t pos;
    uint32_t maxl = std::min( cnthash[currhash], n_rep );

    maxk = 0;
    maxpos = 0xFFFFFFFF;

    for (uint32_t l=0; l<maxl; l++)
    {
        pos = refhash[currhash*n_rep+l];
        if (pos >= (i & 0xFFFF)) pos += (i & 0xFFFF0000) - 65536;
        else pos += (i & 0xFFFF0000);

        if (pos >= i) continue;

        if (((current == load_u32_unaligned(&input[pos])) && ((i - pos - 4) < 0xFFFC)))
        {
            // Calculate the length of this LZ match
            uint32_t k = 4;
            const uint32_t max_match = std::min((uint32_t) (4 + 7 + 255), i - pos);

            while (k < max_match && input[i + k] == input[pos + k])
            {
                k++;
            }

            if (k > maxk) { maxk = k; maxpos = pos; }
        }
    }
}


static inline bool probeLZMatch( uint8_t *input, uint32_t *cnthash, uint16_t *refhash, uint32_t i, uint32_t current, uint32_t currhash, uint32_t n_rep )
{
    uint32_t maxl = std::min( cnthash[currhash], n_rep );

    for (uint32_t l=0; l<maxl; l++)
    {
        uint32_t pos = refhash[currhash*n_rep+l];
        if (pos >= (i & 0xFFFF)) pos += (i & 0xFFFF0000) - 65536;
        else pos += (i & 0xFFFF0000);
        if (pos >= i) continue;
        if (((current == load_u32_unaligned(&input[pos])) && ((i - pos - 4) < 0xFFFC))) return true;
    }

    return false;
}


void tsqEncode2_hist( struct TSQCompressionContextHist* ctx, uint8_t *input, uint8_t *output, uint32_t *outputSize, uint32_t inputSize )
{
    const uint32_t size = inputSize;

    // First write the uncompressed size
    output[0] = (size & 0xFF);
    output[1] = ((size >> 8) & 0xFF);
    output[2] = ((size >> 16) & 0xFF);

    uint32_t i = 0, j = 6;
    uint32_t last_size = j++;
    uint32_t n_sym = 0;
    uint32_t n_rep = ctx->histent;
    uint32_t last_i;
    uint32_t current, currhash;

    printf( "hello\n" );

    do
    {
        bool match;
        last_i = i;

        do
        {
            if ((i + sizeof(uint32_t)) >= size)
            {
                i = size;
                match = false;
                break;
            }

            i++;
            current = load_u32_unaligned(&input[i]);
            currhash = (current ^ (current >> 12)) & TSQ_HASH_HIST_MASK;

            match = probeLZMatch( input, ctx->cnthash, ctx->refhash, i, current, currhash, n_rep );
            ctx->refhash[currhash*n_rep + ctx->cnthash[currhash]%n_rep] = i;
            ctx->cnthash[currhash]++;
        }
        while ((i<size) && !match) ;

        // output literals
        if ((i-last_i) > 0)
        {
            outputLits( input, output, i, last_i, j, last_size, n_sym );
        }

        if (!(i<size)) break;

        // output matches
        do
        {
            uint32_t maxpos, k;

            searchBestLZMatch( input, ctx->cnthash, ctx->refhash, i, current, currhash, n_rep, k, maxpos );

            if ( k < 4 ) break;

            uint8_t low = k > (4+7) ? 7 : (k - 4);
            uint8_t high = k - 11;

            if (k >= (4+7)) output[j++] = high;

            uint32_t offset = i - maxpos;

            output[j++] = offset & 0xFF;
            output[j++] = offset >> 8;

            i += k;

            // Complete the size byte?
            n_sym++;
            output[last_size] = (output[last_size] << 4) | low; if ((n_sym & 1) == 0) { last_size = j++; }

            // Next match? Likely yes when the hash is build up.
            if ((i + sizeof(uint32_t)) > size)
            {
                match = false;
                break;
            }

            current = load_u32_unaligned(&input[i]);
            currhash = (current ^ (current >> 12)) & TSQ_HASH_HIST_MASK;

            match = probeLZMatch( input, ctx->cnthash, ctx->refhash, i, current, currhash, n_rep );
            ctx->refhash[currhash*n_rep + ctx->cnthash[currhash]%n_rep] = i;
            ctx->cnthash[currhash]++;
        }
        while ( (i < size-5) && match) ;

    }
    while (i < size) ;

    // Fill in remaining size byte if odd count
    if ((n_sym & 1) != 0)
    {
        output[last_size] <<= 4;
    }

    output[3] = (n_sym & 0xFF);
    output[4] = ((n_sym >> 8) & 0xFF);
    output[5] = ((n_sym >> 16) & 0xFF);

    *outputSize = j;
}


