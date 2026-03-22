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


void tsqDecode2( uint8_t *inputBlock, uint8_t *outputBlock, uint32_t *outputSize, uint32_t inputSize )
{
    *outputSize = 0;

    // First we read the uncompressed size
    uint32_t size = inputBlock[0] | (inputBlock[1] << 8) | (inputBlock[2] << 16);

    if (!( size <= TSQ_BLOCK_SZ )) return;

    uint32_t n_blocks = inputBlock[3] | (inputBlock[4] << 8) | (inputBlock[5] << 16);
    uint32_t n_fastblocks = (n_blocks - 4) & ~1;
    uint32_t i = 6, j = 0, n = 0;

    // Fast decompression loop that overruns data in the output buffer. We stop close to the end of the data to process more safely.
    while (n < n_fastblocks)
    {
        uint8_t size_byte = inputBlock[i++];
        uint32_t sz = size_byte >> 4;
        uint32_t control = sz & 8;
        uint32_t extra = (sz & 7) == 7;

        sz = control ? (sz & 7) + 1 : (sz & 7) + 4;
        if (extra) sz += inputBlock[i++];

        int32_t pos = j - tsq_read16( &inputBlock[i] );

        /*
        if (control) printf( "i %u j %u lit %u\n", j, i, sz );
        else printf( "i %u j %u rep %u\n", j, i, sz );
        */

        tsq_memcpy8( &outputBlock[j], control ? &inputBlock[i] : &outputBlock[pos], sz );
        
        j += sz;
        i += control ? sz : 2;

        sz = size_byte;
        control = sz & 8;
        extra = (sz & 7) == 7;

        sz = control ? (sz & 7) + 1 : (sz & 7) + 4;
        if (extra) sz += inputBlock[i++];

        pos = j - tsq_read16( &inputBlock[i] );

        /*
        if (control) printf( "i %u j %u lit %u\n", j, i, sz );
        else printf( "i %u j %u rep %u\n", j, i, sz );
        */

        tsq_memcpy8( &outputBlock[j], control ? &inputBlock[i] : &outputBlock[pos], sz );

        j += sz;
        i += control ? sz : 2;

        n += 2;
    }

    // Slow decompression using memcpy but yeilds the correct result without writing over outside of the output buffer/decompressed size.
    while (n < n_blocks)
    {
        uint8_t size_byte = inputBlock[i++];
        uint32_t sz = (size_byte>>4);
        uint32_t control = sz & 8;
        uint32_t extra = (sz & 7) == 7;

        sz = control ? (sz & 7) + 1 : (sz & 7) + 4;
        if (extra) sz += inputBlock[i++];

        int32_t pos = j - tsq_read16( &inputBlock[i] );

        /*
        if (control) printf( "i %u j %u lit %u\n", j, i, sz );
        else printf( "i %u j %u rep %u\n", j, i, sz );
        */

        memcpy(&outputBlock[j], control ? &inputBlock[i] : &outputBlock[pos], sz);
        
        j += sz;
        i += control ? sz : 2;
        n ++;

        if (!(n < n_blocks)) break;

        sz = (size_byte & 15);
        control = sz & 8;
        extra = (sz & 7) == 7;

        sz = control ? (sz & 7) + 1 : (sz & 7) + 4;
        if (extra) sz += inputBlock[i++];

        pos = j - tsq_read16( &inputBlock[i] );

        /*
        if (control) printf( "i %u j %u lit %u\n", j, i, sz );
        else printf( "i %u j %u rep %u\n", j, i, sz );
        */

        memcpy(&outputBlock[j], control ? &inputBlock[i] : &outputBlock[pos], sz);

        j += sz;
        i += control ? sz : 2;
        n ++;
    }

    //printf( "n_sym %u\n", n );

    *outputSize = size;
}


