/*
Libturbosqueeze implementation.

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


#include "turbosqueeze.h"
#include <iostream>
#include <fstream>


#if _MSC_VER
#define align_alloc( A, B ) _aligned_malloc( B, A )
#define align_free( A ) _aligned_free( A )
#else
#define align_alloc( A, B ) aligned_alloc( A, B )
#define align_free( A ) free( A )
#endif


#define MAX_CACHE_LINE_SIZE 256


namespace TurboSqueeze {

    // File Reader declaration
    class FileReader : public IReader {
        std::string filename;
        std::ifstream *infile;
    public:
        FileReader() : filename(), infile(nullptr) {}
        void set(const std::string& file) { filename = file; }
        size_t read(char** buffer, size_t *bufferStart, size_t bufferSize) override;
    };

    // Memory Reader declaration
    class MemoryReader : public IReader {
        char* memoryData;
        size_t memorySize;
        size_t currentPosition;
    public:
        MemoryReader() : memoryData(nullptr), memorySize(0), currentPosition(0) {}
        void set(char* data, size_t size) { memoryData = data; memorySize = size; }
        size_t read(char** buffer, size_t *bufferStart, size_t bufferSize) override;
    };

    // File Writer declaration
    class FileWriter : public IWriter {
        std::string filename;
        std::ofstream *outfile;
        uint8_t *buffer;
        size_t bufferSize;
    public:
        FileWriter() : filename(), outfile(nullptr), buffer(nullptr), bufferSize(0) {}
        void set(const std::string& file) { filename = file; }
        void getdest(char** data, size_t size) override;
        void write() override;
    };

    // Memory Writer declaration
    class MemoryWriter : public IWriter {
        char* memoryData;
        size_t memorySize;
        size_t currentPosition;
        bool overflow;
    public:
        MemoryWriter() : memoryData(nullptr), memorySize(0), currentPosition(0), overflow(false) {}
        void set(char* data, size_t size) { memoryData = data; memorySize = size; }
        void getdest(char** data, size_t size) override;
        void write() override;
        bool isOverflow() const { return overflow; }
    };

    IReader* ReaderFactory( const enum Reader type )
    {
        if (type == Reader::File) return new FileReader();
        else if (type == Reader::Memory) return new MemoryReader();
        return nullptr;
    }

    void ReaderDestroy( IReader* reader )
    {
        delete reader;
    }

    IWriter* WriterFactory( const enum Writer type )
    {
        if (type == Writer::File) return new FileWriter();
        else if (type == Writer::Memory) return new MemoryWriter();
        return nullptr;
    }

    void WriterDestroy( IWriter* writer )
    {
        delete writer;
    }

    // Reader
    size_t FileReader::read(char** buffer, size_t *bufferStart, size_t bufferSize)
    {
        *bufferStart = 0;

        if (!infile)
            infile = new std::ifstream(filename, std::ios::binary);

        if (!infile || !infile.is_open()) return 0;

        if (infile->read(*buffer, bufferSize))
            return bufferSize;
        else
            return 0;
    }

    size_t MemoryReader::read(char** buffer, size_t *bufferStart, size_t bufferSize)
    {
        size_t remaining = memorySize - currentPosition;
        size_t bytesToRead = remaining < bufferSize ? remaining : bufferSize;

        *buffer = memoryData;
        *bufferStart = currentPosition;
        currentPosition += bytesToRead;

        return bytesToRead;
    }

    // Writer
    void FileWriter::getdest(char** data, size_t size)
    {
        if (!buffer) buffer = new uint8_t[TURBOSQUEEZE_OUTPUT_SZ];

        bufferSize = size;

        if (size <= TURBOSQUEEZE_OUTPUT_SZ)
            *data = buffer;
        else
            *data = nullptr;
    }

    void FileWriter::write()
    {
        outfile = new std::ofstream(filename, std::ios::binary);
        if (!outfile) return;
        if (!infile.is_open()) return;
        outfile->write(buffer, bufferSize);
    }

    void MemoryWriter::getdest(char** data, size_t dataSize)
    {
        size_t remaining = memorySize - currentPosition;

        if (dataSize > remaining)
        {
            *data = nullptr;
            overflow = true;
        }
        else
        {
            *data = memoryData + currentPosition;
            currentPosition += dataSize;
        }
    }

    void MemoryWriter::write()
    {
    }

    // Compressor declaration and factory
    class FastCompressor : public ICompressor {
#pragma pack(1)
        struct SymRefFast {
            uint32_t sym4;
            uint32_t latest_pos;
        };
#pragma pack()
        struct SymRefFast *refhash;
        uint8_t *refhashcount;
    public:
        FastCompressor( uint32_t compression_level ) : ICompressor( compression_level ) {}
        void compress(IReader& reader, IWriter& writer) override;
    };

    class FastNCompressor : public ICompressor {
#pragma pack(1)
        struct SymRef {
            uint32_t sym4;
            uint32_t position;
            uint32_t n_occurences;
        };
#pragma pack()
        struct SymRef *hash;
        uint32_t *positions;
        uint8_t *refhashcount;
        uint32_t posIdx;
    public:
        FastNCompressor( uint32_t compression_level ) : ICompressor( compression_level ) {}
        void compress(IReader& reader, IWriter& writer) override;
    };

    class ICompressor* CompressorFactory( uint32_t compression_level )
    {
        if (compression_level>0 && compression_level<=4)
            return new FastNCompressor( compression_level );

        return new FastCompressor();
    }

    void CompressorDestroy( ICompressor* compressor )
    {
        delete compressor
    }

    // Compression helpers

    // Compression method
    void FastCompressor::compress(IReader& reader, IWriter& writer)
    {
        uint8_t* inbuff = (uint8_t*) align_alloc( MAX_CACHE_LINE_SIZE, TURBOSQUEEZE_BLOCK_SZ*sizeof(uint8_t) );
        uint8_t* outbuff = (uint8_t*) align_alloc( MAX_CACHE_LINE_SIZE, TURBOSQUEEZE_OUTPUT_SZ*sizeof(uint8_t) );

        if (!inbuff || !outbuff) return;

        fseek( in, 0, SEEK_END );
        size_t remainsz = ftell( in );
        fseek( in, 0, SEEK_SET );

        while (true)
        {
            // Compress data
            size_t to_read = remainsz > TURBOSQUEEZE_BLOCK_SZ ? TURBOSQUEEZE_BLOCK_SZ : remainsz;

            if ( to_read > 0 && to_read == reader.read( inbuff, to_read ) )
            {
                uint32_t outputSize = 0;

                turbosqueezeEncode( ctx, inbuff, outbuff, &outputSize, to_read );

                writer.write( &outputSize, 3 );
                writer.write( outbuff, outputSize );

                remainsz -= to_read;
            }
            else break;
        }

        if (outbuff != nullptr) align_free(outbuff);
        if (inbuff != nullptr) align_free(inbuff);
    }

    void FastNCompressor::compress(IReader& reader, IWriter& writer)
    {
        uint8_t* inbuff = (uint8_t*) align_alloc( MAX_CACHE_LINE_SIZE, TURBOSQUEEZE_BLOCK_SZ*sizeof(uint8_t) );
        uint8_t* outbuff = (uint8_t*) align_alloc( MAX_CACHE_LINE_SIZE, TURBOSQUEEZE_OUTPUT_SZ*sizeof(uint8_t) );

        if (!inbuff || !outbuff) return;

        size_t remainsz = reader.getSize();

        while (true)
        {
            // Compress data
            size_t to_read = remainsz > TURBOSQUEEZE_BLOCK_SZ ? TURBOSQUEEZE_BLOCK_SZ : remainsz;

            if ( to_read > 0 && to_read == reader.read( inbuff, to_read ) )
            {
                uint32_t outputSize = 0;

                turbosqueezeEncode( ctx, inbuff, outbuff, &outputSize, to_read );

                writer.write( &outputSize, 3 );
                writer.write( outbuff, outputSize );

                remainsz -= to_read;
            }
            else break;
        }

        if (outbuff != nullptr) align_free(outbuff);
        if (inbuff != nullptr) align_free(inbuff);
    }

    // Decompressor
    static bool isLittleEndian()
    {
    #if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
        return true;
    #elif defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
        return false;
    #elif defined(__clang__) && __LITTLE_ENDIAN__
        return true;
    #elif defined(__clang__) && __BIG_ENDIAN__
        return false;
    #elif defined(_MSC_VER) && (_M_AMD64 || _M_IX86)
        return true;
    #elif defined(__DMC__) && defined(_M_IX86)
        return true;
    #else
        static union { uint32_t u; uint8_t c[4]; } one = { 1 };
        return one.c[0] != 0;
    #endif
    }

    class LittleEndianDecompressor : public IDecompressor {
    public:
        void decompress(IReader& reader, IWriter& writer) override;
    };

    class BigEndianDecompressor : public IDecompressor {
    public:
        void decompress(IReader& reader, IWriter& writer) override;
    };

    class AVX2Decompressor : public IDecompressor {
    public:
        void decompress(IReader& reader, IWriter& writer) override;
    };

    IDecompressor* DecompressorFactory()
    {
        if (!isLittleEndian())
            return new BigEndianDecompressor();
#ifdef AVX2
        return new AVX2Decompressor();
#else
        return new LittleEndianDecompressor();
#endif
    }

    void DecompressorDestroy( IDecompressor* decompressor )
    {
        delete decompressor;
    }

    void LittleEndianDecompressor::decompress(IReader& reader, IWriter& writer)
    {
        while (true)
        {
            size_t bytesRead = reader.read(buffer, bufferSize);
            if (bytesRead == 0) break; // No more data to read

            // Compress data
            char* compressedData = nullptr;
            size_t compressedSize = 0;
            // ... compression logic, fill compressedData and compressedSize

            writer.write(compressedData, compressedSize);

            // If less data than buffer size was read, we're done
            if (bytesRead < bufferSize) break;
        }
    }

    // Decompressor
    void BigEndianDecompressor::decompress(IReader& reader, IWriter& writer)
    {
        while (true)
        {
            size_t bytesRead = reader.read(buffer, bufferSize);
            if (bytesRead == 0) break; // No more data to read

            // Compress data
            char* compressedData = nullptr;
            size_t compressedSize = 0;
            // ... compression logic, fill compressedData and compressedSize

            writer.write(compressedData, compressedSize);

            // If less data than buffer size was read, we're done
            if (bytesRead < bufferSize) break;
        }
    }

}
