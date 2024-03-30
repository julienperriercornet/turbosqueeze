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

#include <cstdint>


namespace TurboSqueeze {

    /*
     * Reader interface
     */
    class IReader {
    public:
        virtual ~IReader() {}
        virtual size_t read(char* buffer, size_t *bufferStart, size_t bufferSize) = 0;
    };

    enum class Reader { File, Memory };

    class IReader* ReaderFactory( const enum class Reader type );
    void ReaderDestroy( class IReader* reader );

    /*
     * Writer interface
     */
    class IWriter {
    public:
        virtual ~IWriter() {}
        virtual void write(const char* data, size_t dataSize) = 0;
    };

    enum class Writer { File, Memory };

    class IWriter* WriterFactory( const enum class Writer type );
    void WriterDestroy( class IWriter* writer );

    // File Reader implementation
    class FileReader : public IReader {
        std::string filename;
    public:
        FileReader(const std::string& file) : filename(file) {}
        size_t read(char* buffer, size_t *bufferStart, size_t bufferSize) override;
    };

    // Memory Reader implementation
    class MemoryReader : public IReader {
        const char* memoryData;
        size_t memorySize;
        size_t currentPosition;

    public:
        MemoryReader(const char* data, size_t size) : memoryData(data), memorySize(size), currentPosition(0) {}
        size_t read(char* buffer, size_t *bufferStart, size_t bufferSize) override;
    };

    // File Writer implementation
    class FileWriter : public IWriter {
        std::string filename;
    public:
        FileWriter(const std::string& file) : filename(file) {}
        void write(const char* data, size_t dataSize) override;
    };

    // Memory Writer implementation
    class MemoryWriter : public IWriter {
        char* memoryData;
        size_t memorySize;
        size_t currentPosition;
        bool overflow;

    public:
        MemoryWriter(char* data, size_t size) : memoryData(data), memorySize(size), currentPosition(0), overflow(false) {}
        void write(const char* data, size_t dataSize) override;
        bool isOverflow() const { return overflow; }
    };

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

    class ICompressor* CompressorFactory( uint32_t compression_level );
    void CompressorDestroy( class ICompressor* compressor );

    class FastCompressor : public ICompressor {
    public:
        FastCompressor( uint32_t compression_level ) : ICompressor( compression_level ) {}
        void compress(IReader& reader, IWriter& writer) override;
    };

    class FastNCompressor : public ICompressor {
    public:
        FastNCompressor( uint32_t compression_level ) : ICompressor( compression_level ) {}
        void compress(IReader& reader, IWriter& writer) override;
    };

    /*
     * Decompressor interface
     */
    class IDecompressor {
    public:
        virtual ~IDecompressor() {}
        virtual void decompress(IReader& reader, IWriter& writer) = 0;
    };

    class IDecompressor* DecompressorFactory();
    void DecompressorDestroy( class IDecompressor* decompressor );

    class LittleEndianDecompressor {
    public:
        void decompress(IReader& reader, IWriter& writer) override;
    };

    class BigEndianDecompressor {
    public:
        void decompress(IReader& reader, IWriter& writer) override;
    };

    class AVX2Decompressor {
    public:
        void decompress(IReader& reader, IWriter& writer) override;
    };

}


