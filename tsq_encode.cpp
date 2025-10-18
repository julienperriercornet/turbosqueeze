/*
 * Turbosqueeze encoder.
 *
 * Copyright (c) 2024-2025 Julien Perrier-cornet
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
#include <stdbit.h>

#ifdef AVX2
#if _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
#endif

#include "tsq_encode.h"
#include "tsq_common.h"


static uint8_t mlen[65] = { 0, 0, 0, 0, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2 };


void tsqEncodeNoext( struct TSQCompressionContext* ctx, uint8_t *input, uint8_t *output, uint32_t *outputSize, uint32_t inputSize )
{
    const uint32_t size = inputSize;

    // First write the uncompressed size
    output[0] = (size & 0xFF);
    output[1] = ((size >> 8) & 0xFF);
    output[2] = ((size >> 16) & 0xFF);

    uint32_t i = 0, j = 3;
    uint32_t last_control = j++;
    uint32_t last_size = j++;
    uint32_t currblock = 0;
    uint32_t rep_last_i = 0;
    uint32_t n_sym = 0;
    uint32_t last_i;
    uint32_t current, currhash;
    uint32_t pos, offset;

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
            offset = rep_last_i - pos;

            if ((i-last_i) > 31)
            {
                // output literals
                do
                {
                    uint32_t incr = i-last_i > 16 ? 16 : i-last_i;
                    tsq_memcpy16_compat(&output[j], &input[last_i]);

                    last_i += incr;
                    j += incr;

                    n_sym++;
                    output[last_control] = (output[last_control] << 1) | 1; if ((n_sym & 7) == 0) last_control = j++;
                    output[last_size] = (output[last_size] << 4) | (incr - 1); if ((n_sym & 1) == 0) { last_size = j++; rep_last_i = last_i; }
                }
                while ((i-last_i) > 0) ;                
            }
        }
        while ( (i<size) && !((current == *((uint32_t*) &input[pos])) && ((offset - 4) < 0xFFFB))) ;

        // output literals
        if (((i-last_i) > 0))
        {
            do
            {
                uint32_t incr = i-last_i > 16 ? 16 : i-last_i;
                tsq_memcpy16_compat(&output[j], &input[last_i]);

                last_i += incr;
                j += incr;

                n_sym++;
                output[last_control] = (output[last_control] << 1) | 1; if ((n_sym & 7) == 0) last_control = j++;
                output[last_size] = (output[last_size] << 4) | (incr - 1); if ((n_sym & 1) == 0) { last_size = j++; rep_last_i = last_i; }
            }
            while ((i-last_i) > 0) ;
        }

        if (!(i<size)) break;

        // output matches
        do
        {
            // Calculate the length of this LZ match (It's at least 4 bytes)
            uint64_t* in1 = (uint64_t*) &input[i];
            uint64_t* in2 = (uint64_t*) &input[pos];
            uint64_t xres = (*in1) ^ (*in2);
            uint32_t k = stdc_trailing_zeros_ull( xres ) >> 3, nb;
            if (k==8)
            {
                in1++;
                in2++;
                xres = (*in1) ^ (*in2);
                nb = stdc_trailing_zeros_ull( xres ) >> 3;
                k += nb;
            }

            // Don't overlap with data which hasn't been yet decoded in the decoder.
            k = (k > (rep_last_i - pos)) ? (rep_last_i - pos - 1) : k;
            if ( k < 4 ) break;

            uint32_t matchlen = mlen[k];

            // Output the match offset to current pos
            offset = rep_last_i - pos; // rep_last_i might have changed
            if (!((offset-4) < 0xFFFB)) break;

            //assert( offset == (offset & 0xFFFF) );
            //assert( (offset + k) < rep_last_i );

            output[j++] = offset & 0xFF;
            output[j++] = offset >> 8;
            i += matchlen < 3 ? (matchlen+2) << 4 : matchlen + 1;

            // Complete/flush out control and size bytes?
            n_sym++;
            output[last_control] <<= 1; if ((n_sym & 7) == 0) last_control = j++;
            output[last_size] = (output[last_size] << 4) | matchlen; if ((n_sym & 1) == 0) { last_size = j++; rep_last_i = i; }

            // Next match? Likely yes when the hash is build up.
            current = *((uint32_t*) &input[i]);
            currhash = (current ^ (current >> 12)) & TSQ_HASH_MASK;
            pos = ctx->refhash[currhash];
            if (pos >= (i & 0xFFFF)) pos += (i & 0xFFFF0000) - 65536;
            else pos += (i & 0xFFFF0000);
            ctx->refhash[currhash] = i;
            offset = rep_last_i - pos;
        }
        while ( (i < size-5) && ((current == *((uint32_t*) &input[pos])) && ((offset-4) < 0xFFFB))) ;

    }
    while (i < size) ;

    // Fill in remaining size and controls bytes
    bool last_size_complete = false;

    while (!((n_sym & 7) == 0))
    {
        output[last_control] = output[last_control] << 1 | 1;
        if (!last_size_complete && (n_sym & 1) != 0) {
            output[last_size] <<= 4;
            last_size_complete = true;
        }
        n_sym++;
    }

    *outputSize = j;
}


extern "C" void tsqEncode( struct TSQCompressionContext* ctx, uint8_t *input, uint8_t *output, uint32_t *outputSize, uint32_t inputSize, uint32_t withExtensions )
{
    if (!withExtensions)
    {
        tsqEncodeNoext( ctx, input, output, outputSize, inputSize );
        return;
    }

    const uint32_t size = inputSize;

    // First write the uncompressed size
    output[0] = (size & 0xFF);
    output[1] = ((size >> 8) & 0xFF);
    output[2] = ((size >> 16) & 0xFF);

    uint32_t i = 0, j = 3;
    uint32_t last_control = j++;
    uint32_t last_size = j++;
    uint32_t currblock = 0;
    uint32_t rep_last_i = 0;
    uint32_t n_sym = 0;
    uint32_t last_i;
    uint32_t current, currhash;
    uint32_t pos, offset;

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
            offset = rep_last_i - pos;

            if ((i-last_i) > 31)
            {
                // output literals
                do
                {
                    uint32_t incr = i-last_i > 16 ? 16 : i-last_i;
                    tsq_memcpy16_compat(&output[j], &input[last_i]);

                    last_i += incr;
                    j += incr;

                    n_sym++;
                    output[last_control] = (output[last_control] << 1) | 1; if ((n_sym & 7) == 0) last_control = j++;
                    output[last_size] = (output[last_size] << 4) | (incr - 1); if ((n_sym & 1) == 0) { last_size = j++; rep_last_i = last_i; }
                }
                while ((i-last_i) > 0) ;                
            }
        }
        while ( (i<size) && !((current == *((uint32_t*) &input[pos])) && ((offset - 4) < 0xFFFB))) ;

        // output literals
        if (((i-last_i) > 0))
        {
            do
            {
                uint32_t incr = i-last_i > 16 ? 16 : i-last_i;
                tsq_memcpy16_compat(&output[j], &input[last_i]);

                last_i += incr;
                j += incr;

                n_sym++;
                output[last_control] = (output[last_control] << 1) | 1; if ((n_sym & 7) == 0) last_control = j++;
                output[last_size] = (output[last_size] << 4) | (incr - 1); if ((n_sym & 1) == 0) { last_size = j++; rep_last_i = last_i; }
            }
            while ((i-last_i) > 0) ;
        }

        if (!(i<size)) break;

        // output matches
        do
        {
            // Calculate the length of this LZ match (It's at least 4 bytes)
            uint64_t* in1 = (uint64_t*) &input[i];
            uint64_t* in2 = (uint64_t*) &input[pos];
            uint64_t xres = (*in1) ^ (*in2);
            uint32_t k = stdc_trailing_zeros_ull( xres ) >> 3, nb;
            if (k==8)
            {
                do
                {
                    in1++;
                    in2++;
                    xres = (*in1) ^ (*in2);
                    nb = stdc_trailing_zeros_ull( xres ) >> 3;
                    k += nb;
                } while (nb == 8 && k < 64) ;
            }

            // Don't overlap with data which hasn't been yet decoded in the decoder.
            k = (k > (rep_last_i - pos)) ? (rep_last_i - pos - 1) : k;

            uint32_t matchlen = mlen[k];

            // Output the match offset to current pos
            if ( k < 4 ) break;

            offset = rep_last_i - pos; // rep_last_i might have changed
            if (!((offset-4) < 0xFFFB)) break;

            //assert( offset == (offset & 0xFFFF) );
            //assert( (offset + k) < rep_last_i );

            output[j++] = offset & 0xFF;
            output[j++] = offset >> 8;
            i += matchlen < 3 ? (matchlen+2) << 4 : matchlen + 1;

            // Complete/flush out control and size bytes?
            n_sym++;
            output[last_control] <<= 1; if ((n_sym & 7) == 0) last_control = j++;
            output[last_size] = (output[last_size] << 4) | matchlen; if ((n_sym & 1) == 0) { last_size = j++; rep_last_i = i; }

            // Next match? Likely yes when the hash is build up.
            current = *((uint32_t*) &input[i]);
            currhash = (current ^ (current >> 12)) & TSQ_HASH_MASK;
            pos = ctx->refhash[currhash];
            if (pos >= (i & 0xFFFF)) pos += (i & 0xFFFF0000) - 65536;
            else pos += (i & 0xFFFF0000);
            ctx->refhash[currhash] = i;
            offset = rep_last_i - pos;
        }
        while ( (i < size-5) && ((current == *((uint32_t*) &input[pos])) && ((offset-4) < 0xFFFB))) ;

    }
    while (i < size) ;

    // Fill in remaining size and controls bytes
    bool last_size_complete = false;

    while (!((n_sym & 7) == 0))
    {
        output[last_control] = output[last_control] << 1 | 1;
        if (!last_size_complete && (n_sym & 1) != 0) {
            output[last_size] <<= 4;
            last_size_complete = true;
        }
        n_sym++;
    }

    *outputSize = j;
}

