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
#include <chrono>
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
    double compression_sec = 0.0;

    if (input)
    {
        struct TSQCompressionContext_MT* ctx = tsqAllocateContextCompression_MT( 16, false );

        if (ctx)
        {
            auto comp_start = std::chrono::steady_clock::now();
            tsqCompress_MT( ctx, (uint8_t*) input, infilesize, false, (uint8_t**) &compressed, &compressed_sz, false, 2, 0 );
            auto comp_end = std::chrono::steady_clock::now();
            compression_sec = std::chrono::duration<double>(comp_end - comp_start).count();
            tsqDeallocateContextCompression_MT( ctx );
        }
    }

    char *decompressed = nullptr;
    size_t decompressed_sz = 0;
    double decompression_sec = 0.0;

    if (compressed)
    {
        struct TSQDecompressionContext_MT* dctx = tsqAllocateContextDecompression_MT( 16, false );

        if (dctx)
        {
            auto decomp_start = std::chrono::steady_clock::now();
            tsqDecompress_MT( dctx, (uint8_t*) compressed, compressed_sz, false, (uint8_t**) &decompressed, &decompressed_sz, false );
            auto decomp_end = std::chrono::steady_clock::now();
            decompression_sec = std::chrono::duration<double>(decomp_end - decomp_start).count();
            tsqDeallocateContextDecompression_MT( dctx );
        }
    }

    printf( "MT input: %s (%u) -> (%u) -> (%u)\n", infilename, (uint32_t) infilesize, (uint32_t) compressed_sz, (uint32_t) decompressed_sz );

    bool mt_output_correct = decompressed_sz == infilesize && memcmp( input, decompressed, std::min( decompressed_sz, infilesize ) ) == 0;

    printf( "MT output_correct: %u\n", mt_output_correct );

    printf( "MT compression speed: %.3f MB/s decompression speed: %.3f MB/s\n",
        compression_sec > 0.0 ? (double(infilesize)/compression_sec)/1000000.0 : 0.0,
        decompression_sec > 0.0 ? (double(decompressed_sz)/decompression_sec)/1000000.0 : 0.0 );
    printf( "MT compression time: %.3f s decompression time: %.3f s\n", compression_sec, decompression_sec );

    free( compressed );
    free( decompressed );

    size_t st_compressed_sz = 0;
    size_t st_decompressed_sz = 0;
    double st_compression_sec = 0.0;
    double st_decompression_sec = 0.0;
    bool st_output_correct = false;

    uint32_t n_blocks = (uint32_t) ((infilesize + TSQ_BLOCK_SZ - 1) / TSQ_BLOCK_SZ);
    std::vector<uint8_t*> st_blocks( n_blocks, nullptr );
    std::vector<uint32_t> st_blocksz( n_blocks, 0 );
    uint8_t* st_decompressed = (uint8_t*) malloc( infilesize + 32 );

    struct TSQCompressionContext* st_ctx = tsqAllocateContext();

    if (st_ctx && st_decompressed)
    {
        bool st_ok = true;

        auto st_comp_start = std::chrono::steady_clock::now();

        for (uint32_t b = 0; b < n_blocks && st_ok; b++)
        {
            uint32_t offset = b * TSQ_BLOCK_SZ;
            uint32_t to_read = std::min( (size_t) TSQ_BLOCK_SZ, infilesize - offset );
            uint32_t outsz = 0;
            st_blocks[b] = (uint8_t*) malloc( TSQ_OUTPUT_SZ );

            if (!st_blocks[b])
            {
                st_ok = false;
                break;
            }

            tsqInit( st_ctx );
            tsqEncode2_fast( st_ctx, (uint8_t*) input + offset, st_blocks[b], &outsz, to_read );
            st_blocksz[b] = outsz;
            st_compressed_sz += outsz;
        }

        auto st_comp_end = std::chrono::steady_clock::now();
        st_compression_sec = std::chrono::duration<double>(st_comp_end - st_comp_start).count();

        auto st_decomp_start = std::chrono::steady_clock::now();

        for (uint32_t b = 0; b < n_blocks && st_ok; b++)
        {
            uint32_t offset = b * TSQ_BLOCK_SZ;
            uint32_t to_read = std::min( (size_t) TSQ_BLOCK_SZ, infilesize - offset );
            uint32_t outsz = 0;

            if (!st_blocks[b])
            {
                st_ok = false;
                break;
            }

            tsqDecode2( st_blocks[b], st_decompressed + offset, &outsz, st_blocksz[b] );
            if (outsz != to_read)
            {
                st_ok = false;
                break;
            }

            st_decompressed_sz += outsz;
        }

        auto st_decomp_end = std::chrono::steady_clock::now();
        st_decompression_sec = std::chrono::duration<double>(st_decomp_end - st_decomp_start).count();

        st_output_correct = st_ok && st_decompressed_sz == infilesize && memcmp( input, st_decompressed, infilesize ) == 0;
    }

    for (uint32_t b = 0; b < n_blocks; b++)
    {
        if (st_blocks[b]) free( st_blocks[b] );
    }

    if (st_ctx) tsqDeallocateContext( st_ctx );
    if (st_decompressed) free( st_decompressed );

    printf( "ST input: %s (%u) -> (%u) -> (%u)\n", infilename, (uint32_t) infilesize, (uint32_t) st_compressed_sz, (uint32_t) st_decompressed_sz );
    printf( "ST output_correct: %u\n", st_output_correct );
    printf( "ST compression speed: %.3f MB/s decompression speed: %.3f MB/s\n",
        st_compression_sec > 0.0 ? (double(infilesize)/st_compression_sec)/1000000.0 : 0.0,
        st_decompression_sec > 0.0 ? (double(st_decompressed_sz)/st_decompression_sec)/1000000.0 : 0.0 );
    printf( "ST compression time: %.3f s decompression time: %.3f s\n", st_compression_sec, st_decompression_sec );

    free( input );
}


static void print_usage(const char* progname)
{
    printf("\nUsage:\n"
        "  %s -c[:level] input output [--version1]\n"
        "  %s --compress[:level] input output [--version1]\n"
        "  %s -d input output\n"
        "  %s --decompress input output\n"
        "  %s -b | --benchmark\n"
        "  %s -h | --help\n"
        "\nOptions:\n"
        "  -c, --compress[:level]  Compress input to output (level 0-6, default 0)\n"
        "  -d, --decompress        Decompress input to output\n"
        "  -b, --benchmark         Run benchmark\n"
        "      --version1          Use version 1 format (no extensions)\n"
        "  -h, --help              Show this help message\n",
        progname, progname, progname, progname, progname, progname);
}


int main( int argc, const char** argv )
{
    printf("TurboSqueeze (tsq) v2.0\n"
    "(c) 2024-2026, Julien Perrier-cornet. Free software under MIT Licence.\n");

    enum { MODE_NONE, MODE_COMPRESS, MODE_DECOMPRESS, MODE_BENCHMARK } mode = MODE_NONE;
    int compression_level = 0;
    bool version1 = false;
    const char* input_file = nullptr;
    const char* output_file = nullptr;

    for (int i = 1; i < argc; i++)
    {
        const char* arg = argv[i];

        if (arg[0] == '-' && arg[1] == '-')
        {
            // Long options
            if (strncmp(arg, "--compress", 10) == 0)
            {
                mode = MODE_COMPRESS;
                if (arg[10] == ':')
                    compression_level = atoi(&arg[11]);
            }
            else if (strcmp(arg, "--decompress") == 0)
            {
                mode = MODE_DECOMPRESS;
            }
            else if (strcmp(arg, "--benchmark") == 0)
            {
                mode = MODE_BENCHMARK;
            }
            else if (strcmp(arg, "--version1") == 0)
            {
                version1 = true;
            }
            else if (strcmp(arg, "--help") == 0)
            {
                print_usage(argv[0]);
                return 0;
            }
            else
            {
                fprintf(stderr, "Unknown option: %s\n", arg);
                print_usage(argv[0]);
                return 1;
            }
        }
        else if (arg[0] == '-')
        {
            // Short options: single character after '-'
            switch (arg[1])
            {
                case 'c':
                    mode = MODE_COMPRESS;
                    if (arg[2] == ':')
                        compression_level = atoi(&arg[3]);
                    break;
                case 'd':
                    mode = MODE_DECOMPRESS;
                    break;
                case 'b':
                    mode = MODE_BENCHMARK;
                    break;
                case 'h':
                    print_usage(argv[0]);
                    return 0;
                default:
                    fprintf(stderr, "Unknown option: %s\n", arg);
                    print_usage(argv[0]);
                    return 1;
            }
        }
        else
        {
            // Positional arguments
            if (!input_file)
                input_file = arg;
            else if (!output_file)
                output_file = arg;
            else
            {
                fprintf(stderr, "Unexpected argument: %s\n", arg);
                print_usage(argv[0]);
                return 1;
            }
        }
    }

    if (mode == MODE_NONE)
    {
        print_usage(argv[0]);
        return 1;
    }

    if (mode == MODE_COMPRESS)
    {
        if (!input_file || !output_file)
        {
            fprintf(stderr, "Compression requires input and output file arguments.\n");
            print_usage(argv[0]);
            return 1;
        }

        struct TSQCompressionContext_MT* ctx = tsqAllocateContextCompression_MT( 16, true );

        if (!ctx)
        {
            fprintf(stderr, "Failed to allocate compression context.\n");
            return 1;
        }

        size_t outsize = strlen(output_file);
        uint32_t format_version = version1 ? 1 : 2;

        tsqCompress_MT(ctx, (uint8_t*) input_file, strlen(input_file), true,
                       (uint8_t**) &output_file, &outsize, true, format_version, compression_level);

        tsqDeallocateContextCompression_MT(ctx);
    }
    else if (mode == MODE_DECOMPRESS)
    {
        if (!input_file || !output_file)
        {
            fprintf(stderr, "Decompression requires input and output file arguments.\n");
            print_usage(argv[0]);
            return 1;
        }

        struct TSQDecompressionContext_MT* ctx = tsqAllocateContextDecompression_MT( 16, true );

        if (!ctx)
        {
            fprintf(stderr, "Failed to allocate decompression context.\n");
            return 1;
        }

        size_t outsize = strlen(output_file);

        tsqDecompress_MT(ctx, (uint8_t*) input_file, strlen(input_file), true,
                         (uint8_t**) &output_file, &outsize, true);

        tsqDeallocateContextDecompression_MT(ctx);
    }
    else if (mode == MODE_BENCHMARK)
    {
        benchmark();
    }

    return 0;
}

