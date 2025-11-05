/*
 * Turbosqueeze implementation.
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
#include <cassert>
#include <time.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <algorithm>

#ifdef AVX2
#if _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
#endif

#include "turbosqueeze.h"
#include "platform.h"
#include "tsq_common.h"


extern "C" void tsqCompress( FILE* in, FILE* out, bool use_extentions, uint32_t level )
{
    uint8_t* inbuff = (uint8_t*) align_alloc( MAX_CACHE_LINE_SIZE, TSQ_BLOCK_SZ*sizeof(uint8_t) );
    uint8_t* outbuff = (uint8_t*) align_alloc( MAX_CACHE_LINE_SIZE, TSQ_OUTPUT_SZ*sizeof(uint8_t) );

    struct TSQCompressionContext* ctx = tsqAllocateContext();

    if (ctx && inbuff && outbuff)
    {
        fseek( in, 0, SEEK_END );
        uint64_t remainsz = ftell( in );
        fseek( in, 0, SEEK_SET );

        uint32_t n_blocks = (remainsz % TSQ_BLOCK_SZ != 0 ? 1 : 0) + (remainsz / TSQ_BLOCK_SZ);

        // --- Write TSQ1 header ---
        fwrite("TSQ1", 1, 4, out);
        uint32_t n_blocks_le = n_blocks;
        fwrite(&n_blocks_le, 1, 4, out);
        fwrite(&remainsz, 1, sizeof(uint64_t), out);

        size_t to_read = remainsz > TSQ_BLOCK_SZ ? TSQ_BLOCK_SZ : remainsz;

        while ( to_read > 0 && to_read == fread( inbuff, 1, to_read, in ) )
        {
            uint32_t outputSize;

            tsqInit( ctx );
            tsqEncode( ctx, inbuff, outbuff, &outputSize, to_read, use_extentions );

            uint32_t real_out_size = outputSize;
            if (use_extentions) outputSize += 0x800000;

            fputc(outputSize & 0xFF, out);
            fputc(((outputSize >> 8) & 0xFF), out);
            fputc(((outputSize >> 16) & 0xFF), out);
            fwrite( outbuff, 1, real_out_size, out );
            remainsz -= to_read;
            to_read = remainsz > TSQ_BLOCK_SZ ? TSQ_BLOCK_SZ : remainsz;
        }

    }

    if (ctx) tsqDeallocateContext(ctx);

    if (outbuff != nullptr) align_free(outbuff);
    if (inbuff != nullptr) align_free(inbuff);
}


extern "C" void tsqDecompress( FILE* in, FILE* out )
{
    uint8_t* inbuff = (uint8_t*) align_alloc( MAX_CACHE_LINE_SIZE, 65536+TSQ_OUTPUT_SZ*sizeof(uint8_t) );
    uint8_t* outbuff = (uint8_t*) align_alloc( MAX_CACHE_LINE_SIZE, TSQ_BLOCK_SZ*sizeof(uint8_t)+32 );

    if (!inbuff || !outbuff) return;

    // --- Lire l'en-tête TSQ1 ---
    char magic[4];
    if (fread(magic, 1, 4, in) != 4 || strncmp(magic, "TSQ1", 4) != 0) {
        return;
    }
    uint32_t n_blocks = 0;
    if (fread(&n_blocks, 1, 4, in) != 4) {
        return;
    }
    uint64_t total_uncompressed = 0;
    if (fread(&total_uncompressed, 1, sizeof(uint64_t), in) != sizeof(uint64_t)) {
        return;
    }

    do
    {
        uint32_t to_read = fgetc(in);
        to_read += fgetc(in) << 8;
        to_read += fgetc(in) << 16;

        uint32_t extensions = to_read & 0x800000;
        to_read &= 0x7FFFFF;

        /*
         * Here we read at offset 65536 in the input buffer in order to prevent any buffer underflow access in case we decode a
         * corrupt stream and index refer to memory before the start of the file.
         * One could argue that a better way to check for input buffer boundaries could be in the decompression inner loop, but
         * any extra check there has an actual *cost* in terms of performance.
         * Furthemore we could use that as a feature to store a pre-determinded dictionnary before the start of the file for each 
         * file type to improve the compression... 
         */
        if (to_read > 0 && to_read < TSQ_OUTPUT_SZ && to_read == fread( inbuff+65536, 1, to_read, in ))
        {
            uint32_t outputSize;
            tsqDecode( inbuff+65536, outbuff, &outputSize, to_read, extensions!=0 );
            fwrite( outbuff, 1, outputSize, out );
        }
    }
    while ( !feof(in) ) ;

    if (outbuff != nullptr) align_free(outbuff);
    if (inbuff != nullptr) align_free(inbuff);
}
