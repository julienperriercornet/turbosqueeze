#pragma once

/*
libturbosqueeze header.

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


#include <iostream>
#include <cstdint>


namespace TurboSqueeze {

    /*
     * Reader interface
     */
    class IReader {
    public:
        virtual ~IReader() {}
        virtual size_t read(char** buffer, size_t *bufferStart, size_t bufferSize) = 0;
    };

    enum class Reader { File, Memory };

    IReader* ReaderFactory( const enum Reader type );
    void ReaderDestroy( IReader* reader );

    /*
     * Writer interface
     */
    class IWriter {
    public:
        virtual ~IWriter() {}
        virtual void getdest(char** data, size_t size) = 0;
        virtual void write() = 0;
    };

    enum class Writer { File, Memory };

    IWriter* WriterFactory( const enum Writer type );
    void WriterDestroy( IWriter* writer );

    /*
     * Compressor interface
     */
    class ICompressor {
        uint32_t compressionLevel;
    public:
        ICompressor( uint32_t compression_level ) : compressionLevel( compression_level ) {}
        virtual ~ICompressor() {}
        virtual void compress(IReader& reader, IWriter& writer) = 0;
    };

    ICompressor* CompressorFactory( uint32_t compression_level );
    void CompressorDestroy( ICompressor* compressor );

    /*
     * Decompressor interface
     */
    class IDecompressor {
    public:
        virtual ~IDecompressor() {}
        virtual void decompress(IReader& reader, IWriter& writer) = 0;
    };

    IDecompressor* DecompressorFactory();
    void DecompressorDestroy( IDecompressor* decompressor );

}


