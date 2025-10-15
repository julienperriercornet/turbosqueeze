#pragma once

/*
** Turbosqueeze general API include file.
** Copyright (C) 2024-2025 Nulang solutions developers team
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


#include <cstdint>
#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>


#define TSQ_BLOCK_BITS (22)
#define TSQ_BLOCK_SZ (1<<TSQ_BLOCK_BITS)
#define TSQ_OUTPUT_SZ ((1<<TSQ_BLOCK_BITS) + (1<<(TSQ_BLOCK_BITS-2)))

#define TSQ_HASH_BITS (17)
#define TSQ_HASH_SZ ((1<<TSQ_HASH_BITS) * sizeof(uint16_t))
#define TSQ_HASH_MASK ((1<<TSQ_HASH_BITS) - 1)


struct TSQCompressionContext {
    uint16_t *refhash;
};


struct TSQBuffer {

    uint8_t* buffer;
    uint8_t* filebuffer;
    uint32_t size;
    uint32_t ext;
    uint32_t compression_level; // Compression level for this buffer, if applicable

};


struct TSQWorker {

    // Producer consumer design pattern with N input buffers and M output buffers
    std::vector<struct TSQBuffer> inputs;
    uint32_t n_inputs;
    volatile uint32_t currentReadInput;
    volatile uint32_t currentWorkInput;
    std::mutex input_mtx;
    std::condition_variable input_cv;

    std::vector<struct TSQBuffer> outputs;
    uint32_t n_outputs;
    volatile uint32_t currentWorkOutput;
    volatile uint32_t currentWriteOutput;
    std::mutex output_mtx;
    std::condition_variable output_cv;

    uint32_t blocksPerWorker;

};


struct TSQJob {

    // Buffer contains either a filename or an in memory data block to process
    uint8_t* input;
    size_t size;
    bool input_file; // false = process memory block
    uint32_t jobid; // job ID for async processing
    bool use_extensions;
    uint32_t compression_level;
    FILE* input_stream;
    size_t input_size; // Size of the input data

    // Output buffer
    uint8_t* output;
    size_t outsize;
    bool output_file; // false = write to memory block

    std::function<void(uint32_t jobid, bool)> completion_cb; // completion callback
    std::function<void(uint32_t jobid, double)> progress_cb; // progress callback

};


class TSQCompressionContext_MT {
public:

    TSQCompressionContext_MT() : num_cores(1), workers(nullptr), currentjob(nullptr), threads(nullptr), reader(nullptr), writer(nullptr),
        reader_mtx(), reader_cv(), blocks_writen(0), input_blocks(0), queue(nullptr), queue_mtx(), queue_cv(), maxjobid(1), exit_request(false),
        verbose(false) {}

    uint32_t num_cores;
    struct TSQWorker* workers;

    struct TSQJob *currentjob;

    std::thread** threads;
    std::thread* reader;
    std::thread* writer;

    std::mutex reader_mtx;
    std::condition_variable reader_cv;

    // progress
    uint32_t blocks_writen;
    uint32_t input_blocks;

    // Job queue
    std::queue<struct TSQJob*> *queue;
    std::mutex queue_mtx;
    std::condition_variable queue_cv;
    uint32_t maxjobid;

    // Exit
    bool exit_request;
    bool verbose;

};


class TSQDecompressionContext_MT {
public:

    TSQDecompressionContext_MT() : num_cores(1), workers(nullptr), currentjob(nullptr), threads(nullptr), reader(nullptr), writer(nullptr),
        reader_mtx(), reader_cv(), blocks_writen(0), input_blocks(0), queue(nullptr), queue_mtx(), queue_cv(), maxjobid(1), exit_request(false), verbose(false) {}

    uint32_t num_cores;
    struct TSQWorker* workers;

    struct TSQJob *currentjob;

    std::thread** threads;
    std::thread* reader;
    std::thread* writer;

    std::mutex reader_mtx;
    std::condition_variable reader_cv;

    // progress
    uint32_t blocks_writen;
    uint32_t input_blocks;

    // Job queue
    std::queue<struct TSQJob*> *queue;
    std::mutex queue_mtx;
    std::condition_variable queue_cv;
    uint32_t maxjobid;

    // Exit
    bool exit_request;
    bool verbose;

};


#if defined (__cplusplus)
extern "C" {
#endif

    // High Level File to File API
    void compress( FILE* in, FILE* out, bool useextensions, uint32_t level );
    void decompress( FILE* in, FILE* out );

    // Multithreaded API
    struct TSQCompressionContext_MT* tsqAllocateContextCompression_MT( bool verbose );
    void tsqDeallocateContextCompression_MT(struct TSQCompressionContext_MT* ctx);
    bool tsqCompress_MT( TSQCompressionContext_MT* ctx, uint8_t* in, size_t szin, bool infile, uint8_t** out, size_t *szout, bool outfile, bool useextensions, uint32_t level );

    struct TSQDecompressionContext_MT* tsqAllocateContextDecompression_MT( bool verbose );
    void tsqDeallocateContextDecompression_MT(struct TSQDecompressionContext_MT* ctx);
    bool tsqDecompress_MT( TSQDecompressionContext_MT* ctx, uint8_t* in, size_t szin, bool infile, uint8_t** out, size_t* szout, bool outfile );

    // Low Level API
    struct TSQCompressionContext* tsqAllocateContext();
    void tsqDeallocateContext(struct TSQCompressionContext* ctx);

    void tsqEncode( struct TSQCompressionContext* ctx, uint8_t *inputBlock, uint8_t *outputBlock, uint32_t *outputSize, uint32_t inputSize, uint32_t withExtensions );
    void tsqDecode( uint8_t *inputBlock, uint8_t *outputBlock, uint32_t *outputSize, uint32_t inputSize, uint32_t withExtensions );

#if defined (__cplusplus)
}
#endif

