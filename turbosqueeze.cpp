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


#define TURBOSQUEEZE_BLOCK_BITS (18)
#define TURBOSQUEEZE_BLOCK_SZ (1<<TURBOSQUEEZE_BLOCK_BITS)
#define TURBOSQUEEZE_OUTPUT_SZ ((1<<TURBOSQUEEZE_BLOCK_BITS) + (1<<(TURBOSQUEEZE_BLOCK_BITS-2)))


#define TURBOSQUEEZE_REFHASH_BITS (TURBOSQUEEZE_BLOCK_BITS-1)
#define TURBOSQUEEZE_REFHASH_SZ (1<<TURBOSQUEEZE_REFHASH_BITS)
#define TURBOSQUEEZE_REFHASH_PLUS_SZ (1<<TURBOSQUEEZE_BLOCK_BITS)
#define TURBOSQUEEZE_REFHASH_ENTITIES (4)
#define TURBOSQUEEZE_MAX_SYMS (1<<(TURBOSQUEEZE_BLOCK_BITS-3))


#define turbosqueeze_memcpy8( A, B ) *((uint64_t*) A) = *((const uint64_t*) B)


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

        if (!infile || !infile->is_open()) return 0;

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
            *data = (char*) buffer;
        else
            *data = nullptr;
    }

    void FileWriter::write()
    {
        outfile = new std::ofstream(filename, std::ios::binary);
        if (!outfile) return;
        if (!outfile->is_open()) return;
        outfile->write((const char*) buffer, bufferSize);
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
        ~FastNCompressor();
    };

    class ICompressor* CompressorFactory( uint32_t compression_level )
    {
        if (compression_level>0 && compression_level<=4)
            return new FastNCompressor( compression_level );

        return new FastCompressor( 0 );
    }

    void CompressorDestroy( ICompressor* compressor )
    {
        delete compressor;
    }

    // Compression helpers

    // Compression method
    void ICompressor::compress(IReader& reader, IWriter& writer)
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

                encode( inbuff, outbuff, &outputSize, to_read );

                writer.write( &outputSize, 3 );
                writer.write( outbuff, outputSize );

                remainsz -= to_read;
            }
            else break;
        }

        if (outbuff != nullptr) align_free(outbuff);
        if (inbuff != nullptr) align_free(inbuff);
    }

    void ICompressor::encode( uint8_t *inputBlock, uint8_t *outputBlock, uint32_t *outputSize, uint32_t inputSize )
    {
        const uint32_t size = inputSize;

        // First write the uncompressed size
        outputBlock[0] = (size & 0xFF);
        outputBlock[1] = ((size >> 8) & 0xFF);
        outputBlock[2] = ((size >> 16) & 0xFF);

        *outputSize = 3;

        init();

        uint32_t entryPos = 0;
        struct seqEntry entryBuffer[9];

        uint32_t i = 0;
        uint32_t j = 3;
        uint32_t last_i = i;
        uint32_t rep_last_i = i;
        uint8_t *outptr = outputBlock;

        while (i < size)
        {
            bool hit = false;
            uint32_t hitlength = -1;
            uint32_t hitpos = -1;

            last_i = i;

            // Count NoHit characters
            while ((i < size) && ((i-last_i) < 16))
            {
                hit = addHit( ctx, inputBlock, i, rep_last_i, size, hitlength, hitpos );
                hit = hit && ((rep_last_i - hitpos) < ((1<<16) - 32)) && ((hitpos + hitlength) < rep_last_i);
                if (hit) break;
                i++;
            }

            // Litterals
            if ((i-last_i) > 0)
            {
                entryBuffer[entryPos].repeat = false;
                entryBuffer[entryPos].size = i-last_i;
                entryBuffer[entryPos].position = last_i;
                entryBuffer[entryPos].base = rep_last_i;
                entryPos++;

                if ((entryPos & 1) == 0)
                    rep_last_i = i;
            }

            // Repeat
            if (hit)
            {
                entryBuffer[entryPos].repeat = true;
                entryBuffer[entryPos].size = hitlength;
                entryBuffer[entryPos].position = hitpos;
                entryBuffer[entryPos].base = rep_last_i;

                entryPos++;

                i += hitlength;

                if ((entryPos & 1) == 0)
                    rep_last_i = i;
            }

            // Write output/flush?
            if (entryPos >= 8)
            {
                j += writeOutput( &entryBuffer[0], &entryPos, outptr+j, inputBlock, false, j );
            }
        }

        // Remaining litterals at the end of the buffer?
        if ((i-last_i) > 0)
        {
            entryBuffer[entryPos].repeat = false;
            entryBuffer[entryPos].size = i-last_i;
            entryBuffer[entryPos].position = last_i;
            entryBuffer[entryPos].base = rep_last_i;
            entryPos++;
        }

        // Finalize stream
        j += writeOutput( &entryBuffer[0], &entryPos, outptr+j, inputBlock, true, j );

        *outputSize = j;
    }

    static inline uint32_t getHash( uint32_t h )
    {
        return (((h & (0xFFFFFFFF - (TURBOSQUEEZE_REFHASH_SZ - 1))) >> (32-TURBOSQUEEZE_REFHASH_BITS)) ^ (h & (TURBOSQUEEZE_REFHASH_SZ - 1)));
    }

    static inline uint32_t getHash2( uint32_t h )
    {
        return (((h & (0xFFFFFFFF - (TURBOSQUEEZE_REFHASH_PLUS_SZ - 1))) >> (32-TURBOSQUEEZE_BLOCK_BITS)) ^ (h & (TURBOSQUEEZE_REFHASH_PLUS_SZ - 1)));
    }

    static inline uint32_t matchlen( uint8_t *inbuff, uint32_t first, uint32_t second, uint32_t decoded_size, uint32_t size )
    {
        uint32_t maxmatchstrlen = 16;

        maxmatchstrlen = (first+maxmatchstrlen < decoded_size) ? maxmatchstrlen : decoded_size-first;
        maxmatchstrlen = (second+maxmatchstrlen) < size ? maxmatchstrlen : size - second;
        maxmatchstrlen = (second-first) < maxmatchstrlen ? second-first : maxmatchstrlen;

        if (maxmatchstrlen >= 4)
        {
            uint32_t i = 4;
            uint8_t *strfirst = inbuff+first;
            uint8_t *strsecond = inbuff+second;

            while ((i != maxmatchstrlen) && (strfirst[i] == strsecond[i])) i++;

            return i;
        }
        else
            return 0;
    }

    FastCompressor::FastCompressor( uint32_t compression_level ) : ICompressor( compression_level )
    {
        refhashcount = (uint8_t*) align_alloc( MAX_CACHE_LINE_SIZE, TURBOSQUEEZE_REFHASH_SZ*sizeof(uint8_t) );
        refhash = (FastCompressor::SymRefFast*) align_alloc( MAX_CACHE_LINE_SIZE, TURBOSQUEEZE_REFHASH_SZ*TURBOSQUEEZE_REFHASH_ENTITIES*sizeof(FastCompressor::SymRefFast) );
    }

    ~FastCompressor()
    {
        if (refhash != nullptr) align_free(refhash);
        if (refhashcount != nullptr) align_free(refhashcount);
    }

    void FastCompressor::init()
    {
        memset( ctx->refhashcount, 0, TURBOSQUEEZE_REFHASH_SZ*sizeof(uint8_t) );
    }

    bool FastCompressor::addHit( uint8_t *input, uint32_t i, uint32_t decoded_size, uint32_t size, uint32_t &hitlength, uint32_t &hitpos)
    {
        if (i < size-3)
        {
            uint32_t str4 = *((uint32_t*) (input+i));
            uint32_t hash = getHash(str4);
            uint32_t hitidx = hash*TURBOSQUEEZE_REFHASH_ENTITIES;
            uint32_t j = 0;

            while (j < context->refhashcount[hash] && context->refhash[hitidx].sym4 != str4)
            {
                j++;
                hitidx++;
            }

            if (j < context->refhashcount[hash])
            {
                // Hit sym
                uint32_t matchlength = matchlen( input, context->refhash[hitidx].latest_pos, i, decoded_size, size );

                if (matchlength >= 4)
                {
                    hitlength = matchlength;
                    hitpos = context->refhash[hitidx].latest_pos;

                    context->refhash[hitidx].latest_pos = i;

                    return true;
                }
            }
            else if (j < TURBOSQUEEZE_REFHASH_ENTITIES)
            {
                // New sym
                context->refhash[hitidx].sym4 = str4;
                context->refhash[hitidx].latest_pos = i;

                context->refhashcount[hash]++;
            }
        }

        return false;
    }

    FastNCompressor::FastNCompressor( uint32_t compression_level ) : ICompressor( compression_level )
    {
        refhashcount = (uint8_t*) align_alloc( MAX_CACHE_LINE_SIZE, TURBOSQUEEZE_REFHASH_PLUS_SZ*sizeof(uint8_t) );
        hash = (FastNCompressor::SymRef*) align_alloc( MAX_CACHE_LINE_SIZE, TURBOSQUEEZE_REFHASH_PLUS_SZ*TURBOSQUEEZE_REFHASH_ENTITIES*sizeof(FastNCompressor::SymRef) );
        positions = (uint32_t*) align_alloc( MAX_CACHE_LINE_SIZE, TURBOSQUEEZE_MAX_SYMS*compression_level*8*sizeof(uint32_t) );
    }

    ~FastNCompressor()
    {
        if (refhashcount != nullptr) align_free(refhashcount);
        if (hash != nullptr) align_free(hash);
        if (positions != nullptr) align_free(positions);
    }

    void FastNCompressor::init()
    {
        memset( ctx->refhashcount, 0, TURBOSQUEEZE_REFHASH_PLUS_SZ*sizeof(uint8_t) );
    }

    bool FastNCompressor::addHit( uint8_t *input, uint32_t i, uint32_t decoded_size, uint32_t size, uint32_t &hitlength, uint32_t &hitpos)
    {
        if (i < size-3)
        {
            uint32_t str4 = *((uint32_t*) (input+i));
            uint32_t hash = getHash2(str4);
            uint32_t hitidx = hash*TURBOSQUEEZE_REFHASH_ENTITIES;
            uint32_t j = 0;

            while (j < context->refhashcount[hash] && context->hash[hitidx].sym4 != str4)
            {
                j++;
                hitidx++;
            }

            if (j < context->refhashcount[hash])
            {
                if (context->hash[hitidx].n_occurences == 1)
                {
                    uint32_t matchlength = matchlen( input, context->hash[hitidx].position, i, decoded_size, size );

                    if (matchlength >= 4)
                    {
                        context->hash[hitidx].n_occurences++;

                        hitlength = matchlength;
                        hitpos = context->hash[hitidx].position;

                        // allocate hits
                        uint32_t firstpos = context->hash[hitidx].position;
                        uint32_t pos = context->hash[hitidx].position = context->posIdx;

                        context->positions[pos] = firstpos;
                        context->positions[pos+1] = i;

                        context->posIdx += context->compressionLevel*8;

                        return true;
                    }
                }
                else
                {
                    uint32_t n_occ = context->hash[hitidx].n_occurences > context->compressionLevel*8 ? context->compressionLevel*8 : context->hash[hitidx].n_occurences;
                    uint32_t pos = context->hash[hitidx].position;
                    uint32_t maxmatchlength = 0;
                    uint32_t maxmatchpos = 0xFFFFFFFF;

                    for (uint32_t k=0; k<n_occ; k++)
                    {
                        if ((decoded_size - context->positions[pos+k]) < ((1<<16) - 32))
                        {
                            uint32_t matchlength = matchlen( input, context->positions[pos+k], i, decoded_size, size );

                            if (matchlength > maxmatchlength)
                            {
                                maxmatchlength = matchlength;
                                maxmatchpos = context->positions[pos+k];
                            }
                        }
                    }

                    if (maxmatchlength >= 4)
                    {
                        context->positions[pos+(context->hash[hitidx].n_occurences%(context->compressionLevel*8))] = i;
                        context->hash[hitidx].n_occurences++;

                        hitlength = maxmatchlength;
                        hitpos = maxmatchpos;

                        return true;
                    }
                }
            }
            else if (j < TURBOSQUEEZE_REFHASH_ENTITIES)
            {
                // New sym
                context->hash[hitidx].sym4 = str4;
                context->hash[hitidx].position = i;
                context->hash[hitidx].n_occurences = 1;

                context->refhashcount[hash]++;
            }
        }

        return false;
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
