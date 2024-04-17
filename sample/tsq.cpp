/*
libturbosqueeze turbosqueeze sample/test console application.

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
#include <cstring>
#include <time.h>


#include "../turbosqueeze.h"


void compress( const char* infilename, const char* outfilename, uint32_t compression_level )
{
    clock_t start = clock();


    printf("%s -> %s in %.3fs\n", infilename, outfilename, double(clock()-start) / CLOCKS_PER_SEC );
}


void decompress( const char* infilename, const char* outfilename )
{
    clock_t start = clock();


    printf("%s -> %s in %.3fs\n", infilename, outfilename, double(clock()-start) / CLOCKS_PER_SEC );
}


void test()
{
    const uint32_t testsize = 1<<30;

    uint8_t* testinput = new uint8_t [testsize];
    uint8_t* testoutput = new uint8_t [testsize+testsize/4];
    uint8_t* testdecompressed = new uint8_t [testsize];

    for (uint32_t i=0; i<testsize; i++)
        testinput[i] = i & 0xF;

    // Compress at level 0
    auto compression_ctx = TurboSqueeze::CompressorFactory( 0 );
    auto memory_reader = static_cast<TurboSqueeze::MemoryReader*>( TurboSqueeze::ReaderFactory( TurboSqueeze::Reader::Memory ) );
    auto memory_writer = static_cast<TurboSqueeze::MemoryWriter*>( TurboSqueeze::WriterFactory( TurboSqueeze::Writer::Memory ) );

    memory_reader->set( (char*) testinput, testsize );
    memory_writer->set( (char*) testoutput, testsize+testsize/4 );

    clock_t start = clock();

    compression_ctx->compress( *memory_reader, *memory_writer );

    printf("Compression level 0 in %.3fs\n", double(clock()-start) / CLOCKS_PER_SEC );

    TurboSqueeze::WriterDestroy( memory_writer );
    memory_writer = nullptr;
    TurboSqueeze::ReaderDestroy( memory_reader );
    memory_reader = nullptr;
    TurboSqueeze::CompressorDestroy( compression_ctx );
    compression_ctx = nullptr;

    // Compress at level 1
    compression_ctx = TurboSqueeze::CompressorFactory( 1 );
    memory_reader = static_cast<TurboSqueeze::MemoryReader*>( TurboSqueeze::ReaderFactory( TurboSqueeze::Reader::Memory ) );
    memory_writer = static_cast<TurboSqueeze::MemoryWriter*>( TurboSqueeze::WriterFactory( TurboSqueeze::Writer::Memory ) );

    memory_reader->set( (char*) testinput, testsize );
    memory_writer->set( (char*) testoutput, testsize+testsize/4 );

    start = clock();

    compression_ctx->compress( *memory_reader, *memory_writer );

    printf("Compression level 1 in %.3fs\n", double(clock()-start) / CLOCKS_PER_SEC );

    TurboSqueeze::WriterDestroy( memory_writer );
    memory_writer = nullptr;
    TurboSqueeze::ReaderDestroy( memory_reader );
    memory_reader = nullptr;
    TurboSqueeze::CompressorDestroy( compression_ctx );
    compression_ctx = nullptr;

    // Decompress
    auto decompression_ctx = TurboSqueeze::DecompressorFactory();
    memory_reader = static_cast<TurboSqueeze::MemoryReader*>( TurboSqueeze::ReaderFactory( TurboSqueeze::Reader::Memory ) );
    memory_writer = static_cast<TurboSqueeze::MemoryWriter*>( TurboSqueeze::WriterFactory( TurboSqueeze::Writer::Memory ) );

    memory_reader->set( (char*) testoutput, testsize );
    memory_writer->set( (char*) testdecompressed, testsize );

    start = clock();

    decompression_ctx->decompress( *memory_reader, *memory_writer );

    printf("Decompression in %.3fs\n", double(clock()-start) / CLOCKS_PER_SEC );
    TurboSqueeze::WriterDestroy( memory_writer );
    memory_writer = nullptr;
    TurboSqueeze::ReaderDestroy( memory_reader );
    memory_reader = nullptr;
    TurboSqueeze::DecompressorDestroy( decompression_ctx );
    decompression_ctx = nullptr;

    delete [] testdecompressed;
    delete [] testoutput;
    delete [] testinput;
}


int main( int argc, const char** argv )
{
    if (argc == 4 && strncmp(argv[1], "-c:", 3) == 0)
        compress(argv[2], argv[3], atoi(argv[1]+3));
    else if (argc == 4 && strncmp(argv[1], "-c", 2) == 0)
        compress(argv[2], argv[3], 0);
    else if (argc == 4 && strncmp(argv[1], "-d", 2) == 0)
        decompress(argv[2], argv[3]);
    else if (argc == 2 && strncmp(argv[1], "-t", 2) == 0)
        test();
    else
    {
        printf("turbosqueeze v1.0\n"
        "(C) 2024, Julien Perrier-cornet. Free software under the BSD 3-clause License.\n"
        "\n"
        "To compress: turbosqueeze -c:0..4 input output\n"
        "To decompress: turbosqueeze -d input output\n"
        );
        return 1;
    }

    return 0;
}
