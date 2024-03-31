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


#if _MSC_VER
#define align_alloc( A, B ) _aligned_malloc( B, A )
#define align_free( A ) _aligned_free( A )
#else
#define align_alloc( A, B ) aligned_alloc( A, B )
#define align_free( A ) free( A )
#endif


#define MAX_CACHE_LINE_SIZE 256


namespace TurboSqueeze {

    // Reader
    size_t FileReader::read(char** buffer, size_t *bufferStart, size_t bufferSize) override
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

    size_t MemoryReader::read(char** buffer, size_t *bufferStart, size_t bufferSize) override
    {
        size_t remaining = memorySize - currentPosition;
        size_t bytesToRead = remaining < bufferSize ? remaining : bufferSize;

        *buffer = memoryData;
        *bufferStart = currentPosition;
        currentPosition += bytesToRead;

        return bytesToRead;
    }

    // Writer
    void FileWriter::getdest(char** data, size_t size) override;
    {
        if (!buffer) buffer = new uint8_t[TURBOSQUEEZE_OUTPUT_SZ];

        bufferSize = size;

        if (size <= TURBOSQUEEZE_OUTPUT_SZ)
            *data = buffer;
        else
            *data = nullptr;
    }

    void FileWriter::write() override;
    {
        outfile = new std::ofstream(filename, std::ios::binary);
        if (!outfile) return;
        if (!infile.is_open()) return;
        outfile->write(buffer, bufferSize);
    }

    void MemoryWriter::getdest(char** data, size_t dataSize) override;
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

    void MemoryWriter::write() override
    {
    }

    // Compression helpers

    // Compression method
    void FastCompressor::compress(IReader& reader, IWriter& writer)
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

    void FastNCompressor::compress(IReader& reader, IWriter& writer)
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
    void LittleEndianDecompressor::decompress(IReader& reader, IWriter& writer) override
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
    void BigEndianDecompressor::decompress(IReader& reader, IWriter& writer) override
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
