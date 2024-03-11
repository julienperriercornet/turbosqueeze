/*
libturbosqueeze turbosqueeze encoder/decoder.

BSD 3-Clause License

Copyright (c) 2024, Julien Perrier-cornet

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <time.h>


#include "../libturbosqueeze.h"
#include "../platform.h"


void compress( FILE* in, FILE* out, const char* infilename, const char* outfilename )
{
    clock_t start = clock();

    uint8_t* inbuff = (uint8_t*) align_alloc( MAX_CACHE_LINE_SIZE, TURBOSQUEEZE_BLOCK_SZ*sizeof(uint8_t) );
    uint8_t* outbuff = (uint8_t*) align_alloc( MAX_CACHE_LINE_SIZE, TURBOSQUEEZE_OUTPUT_SZ*sizeof(uint8_t) );

    struct TSCompressionContext* ctx = turbosqueezeAllocateCompression();

    clock_t accumRead = 1, accumEncode = 0, accumWrite = 0;

    if (ctx && inbuff && outbuff)
    {
        fseek( in, 0, SEEK_END );
        size_t remainsz = ftell( in );
        fseek( in, 0, SEEK_SET );

        size_t to_read = remainsz > TURBOSQUEEZE_BLOCK_SZ ? TURBOSQUEEZE_BLOCK_SZ : remainsz;

        clock_t startread = clock();

        while ( to_read > 0 && to_read == fread( inbuff, 1, to_read, in ) )
        {
            uint32_t outputSize;

            clock_t startencode = clock();

            turbosqueezeEncode( ctx, inbuff, outbuff, &outputSize, to_read );

            clock_t startwrite = clock();

            fputc(outputSize & 0xFF, out);
            fputc(((outputSize >> 8) & 0xFF), out);
            fputc(((outputSize >> 16) & 0xFF), out);
            fwrite( outbuff, 1, outputSize, out );
            remainsz -= to_read;
            to_read = remainsz > TURBOSQUEEZE_BLOCK_SZ ? TURBOSQUEEZE_BLOCK_SZ : remainsz;

            clock_t endwrite = clock();

            accumRead += startencode - startread;
            accumEncode += startwrite - startencode;
            accumWrite += endwrite - startwrite;
        }
    }

    if (ctx) turbosqueezeDeallocateCompression(ctx);

    if (outbuff != nullptr) align_free(outbuff);
    if (inbuff != nullptr) align_free(inbuff);

    printf("%s (%ld) -> %s (%ld) in %.3fs (Write %.3fs %.1fMB/s) (Encode %.3fs %.1fMB/s)\n",
        infilename, ftell(in), outfilename, ftell(out), double(clock()-start) / CLOCKS_PER_SEC,
        double(accumWrite) / CLOCKS_PER_SEC, ftell(out) * 0.000001 / (double(accumWrite) / CLOCKS_PER_SEC),
        double(accumEncode) / CLOCKS_PER_SEC, ftell(in) * 0.000001 / (double(accumEncode) / CLOCKS_PER_SEC));

    fflush(out);
}


void decompress( FILE* in, FILE* out, const char* infilename, const char* outfilename )
{
    clock_t start = clock();

    uint8_t* inbuff = (uint8_t*) align_alloc( MAX_CACHE_LINE_SIZE, TURBOSQUEEZE_OUTPUT_SZ*sizeof(uint8_t) );
    uint8_t* outbuff = (uint8_t*) align_alloc( MAX_CACHE_LINE_SIZE, TURBOSQUEEZE_BLOCK_SZ*sizeof(uint8_t) );

    clock_t accumRead = 0, accumDecode = 0, accumWrite = 0;

    if (inbuff && outbuff)
    {
        do
        {
            uint32_t to_read = fgetc(in);
            to_read += fgetc(in) << 8;
            to_read += fgetc(in) << 16;

            clock_t startread = clock();

            if (to_read > 0 && to_read < TURBOSQUEEZE_OUTPUT_SZ && to_read == fread( inbuff, 1, to_read, in ))
            {
                uint32_t outputSize;

                clock_t startdecode = clock();

                turbosqueezeDecode( inbuff, outbuff, &outputSize, to_read );

                clock_t startwrite = clock();

                fwrite( outbuff, 1, outputSize, out );

                clock_t endwrite = clock();

                accumRead += startdecode - startread;
                accumDecode += startwrite - startdecode;
                accumWrite += endwrite - startwrite;
            }
        }
        while ( !feof(in) ) ;
    }

    if (outbuff != nullptr) align_free(outbuff);
    if (inbuff != nullptr) align_free(inbuff);

    printf("%s (%ld) -> %s (%ld) in %.3fs (Read %.3fs %.1fMB/s) (Write %.3fs %.1fMB/s) (Decode %.3fs %.1fMB/s)\n",
        infilename, ftell(in), outfilename, ftell(out), double(clock()-start) / CLOCKS_PER_SEC,
        double(accumRead) / CLOCKS_PER_SEC, ftell(in) * 0.000001 / (double(accumRead) / CLOCKS_PER_SEC),
        double(accumWrite) / CLOCKS_PER_SEC, ftell(out) * 0.000001 / (double(accumWrite) / CLOCKS_PER_SEC),
        double(accumDecode) / CLOCKS_PER_SEC, ftell(out) * 0.000001 / (double(accumDecode) / CLOCKS_PER_SEC));

    fflush(out);
}


void test( FILE* in, FILE* out, const char* infilename, const char* outfilename )
{
    clock_t start = clock();

    uint8_t* inbuff = (uint8_t*) align_alloc( MAX_CACHE_LINE_SIZE, TURBOSQUEEZE_OUTPUT_SZ*sizeof(uint8_t) );
    uint8_t* outbuff = (uint8_t*) align_alloc( MAX_CACHE_LINE_SIZE, TURBOSQUEEZE_OUTPUT_SZ*sizeof(uint8_t) );

    struct TSCompressionContext* ctx = turbosqueezeAllocateCompression();

    if (ctx && inbuff && outbuff)
    {
        fseek( in, 0, SEEK_END );
        size_t remainsz = ftell( in );
        fseek( in, 0, SEEK_SET );

        size_t to_read = remainsz > TURBOSQUEEZE_BLOCK_SZ ? TURBOSQUEEZE_BLOCK_SZ : remainsz;

        while ( to_read > 0 && to_read == fread( inbuff, 1, to_read, in ) )
        {
            uint32_t compressedSize;

            turbosqueezeEncode( ctx, inbuff, outbuff, &compressedSize, to_read );

            uint32_t uncompressedSize;

            turbosqueezeDecode( outbuff, inbuff, &uncompressedSize, compressedSize );

            assert( uncompressedSize == to_read );

            fwrite( inbuff, 1, uncompressedSize, out );

            remainsz -= to_read;
            to_read = remainsz > TURBOSQUEEZE_BLOCK_SZ ? TURBOSQUEEZE_BLOCK_SZ : remainsz;
        }
    }

    if (ctx) turbosqueezeDeallocateCompression(ctx);

    if (outbuff != nullptr) align_free(outbuff);
    if (inbuff != nullptr) align_free(inbuff);

    fflush(out);
}


int main( int argc, const char** argv )
{
    if (argc != 4)
    {
        printf("turbosqueeze v0.1 alpha\n"
        "(C) 2024, Julien Perrier-cornet. Free software under BSD 3-clause Licence.\n"
        "\n"
#ifndef TURBOSQUEEZE_DEBUG
        "To compress/decompress: turbosqueeze c/d input output\n"
#else
        "Test mode: turbosqueeze t input output\n"
#endif
        );
        return 1;
    }

    FILE *in = fopen(argv[2], "rb");
    if (!in) return 1;

    FILE *out = fopen(argv[3], "wb");
    if (!out) return 1;

#ifndef TURBOSQUEEZE_DEBUG
    if (argv[1][0] == 'c')
        compress(in, out, argv[2], argv[3]);
    else if (argv[1][0] == 'd')
        decompress(in, out, argv[2], argv[3]);
#else
    if (argv[1][0] == 't')
        test(in, out, argv[2], argv[3]);
#endif

    fclose(in);
    fclose(out);

    return 0;
}
