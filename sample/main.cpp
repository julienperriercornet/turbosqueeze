/*
** Turbosqueeze sample.
** Copyright (C) 2024-2025 Julien Perrier-cornet
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program.  If not, see <http://www.gnu.org/licenses/>.
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

#include "../turbosqueeze.h"
#include "../platform.h"
#include "../tsq_common.h"


void benchmark()
{
    char *input = nullptr;
    const char *infilename = "enwik9";
    FILE *infile = fopen( infilename, "rb" );
    size_t infilesize = 0;

    if (infile)
    {
        fseek( infile, 0, SEEK_END );
        infilesize = ftell( infile );
        fseek( infile, 0, SEEK_SET );
        input = (char*) malloc( 256+infilesize*sizeof(char) );
        size_t szread = fread( input, 1, infilesize, infile );
        fclose( infile );
    }
    else
    {
        printf("File: %s not found.\n", infilename);
        return;
    }

    char *compressed = nullptr;
    size_t compressed_sz = 0;
    clock_t comp_start, comp_end;

    if (input)
    {
        struct TSQCompressionContext_MT* ctx = tsqAllocateContextCompression_MT( false );

        if (ctx)
        {
            comp_start = clock();
            tsqCompress_MT( ctx, (uint8_t*) input, infilesize, false, (uint8_t**) &compressed, &compressed_sz, false, false, 0 );
            comp_end = clock();
            tsqDeallocateContextCompression_MT( ctx );
        }
    }

    char *decompressed = nullptr;
    size_t decompressed_sz = 0;
    clock_t decomp_start, decomp_end;

    if (compressed)
    {
        struct TSQDecompressionContext_MT* dctx = tsqAllocateContextDecompression_MT( false );

        if (dctx)
        {
            decomp_start = clock();
            tsqDecompress_MT( dctx, (uint8_t*) compressed, compressed_sz, false, (uint8_t**) &decompressed, &decompressed_sz, false );
            decomp_end = clock();
            tsqDeallocateContextDecompression_MT( dctx );
        }
    }

    printf( "input: %s (%u) -> (%u) -> (%u)\n", infilename, (uint32_t) infilesize, (uint32_t) compressed_sz, (uint32_t) decompressed_sz );

    bool output_correct = memcmp( input, decompressed, std::min( decompressed_sz, infilesize ) ) == 0;

    printf( "output_correct: %u\n", output_correct );

    free( input );
    free( compressed );
    free( decompressed );

    double compression_sec = (double(comp_end-comp_start) / CLOCKS_PER_SEC);
    double decompression_sec = (double(decomp_end-decomp_start) / CLOCKS_PER_SEC);

    printf( "compression speed: %.3f MB/s decompression speed: %.3f MB/s\n",
        (double(infilesize)/compression_sec)/100000.0, (double(decompressed_sz)/decompression_sec)/100000.0 );
}


int main( int argc, const char** argv )
{
    if (!(argc == 4 || argc == 5 || argc == 2))
    {
        printf("TurboSqueeze (tsq) v0.8\n"
        "(c) 2024-2025, Julien Perrier-cornet. Free software under MIT Licence.\n"
        "\n"
        "To compress: tsq c input output (--no-ext)\n"
        "To decompress: tsq d input output\n");
        return 1;
    }

    // Flag for not using extensions
    bool noextensions = argc == 5 && strcmp( "--no-ext", argv[4] ) == 0;

    if ((argc == 4 || argc == 5) && argv[1][0] == 'c')
    {
        struct TSQCompressionContext_MT* ctx = tsqAllocateContextCompression_MT( true );

        if (!ctx)
        {  
            fprintf(stderr, "Failed to allocate compression context.\n");
            return 1;
        }

        size_t outsize = strlen(argv[3]);

        tsqCompress_MT(ctx, (uint8_t*) argv[2], strlen(argv[2]), true, (uint8_t**) &argv[3], &outsize, true, !noextensions, 0);

        tsqDeallocateContextCompression_MT(ctx);
    }
    else if ((argc == 4 || argc == 5) && argv[1][0] == 'd')
    {
        struct TSQDecompressionContext_MT* ctx = tsqAllocateContextDecompression_MT( true );

        if (!ctx)
        {
            fprintf(stderr, "Failed to allocate decompression context.\n");
            return 1;
        }
        
        size_t outsize = strlen(argv[3]);

        tsqDecompress_MT(ctx, (uint8_t*) argv[2], strlen(argv[2]), true, (uint8_t**) &argv[3], &outsize, true);

        tsqDeallocateContextDecompression_MT(ctx);
    }
    else if ((argc == 2) && argv[1][0] == 'b')
    {
        benchmark();
    }

    return 0;
}

