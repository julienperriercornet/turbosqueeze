/*
 * Turbosqueeze context implementation.
 *
 * Copyright (c) 2024-2025 Julien Perrier-cornet
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
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

#include "turbosqueeze.h"
#include "platform.h"
#include "tsq_common.h"


extern "C" void tsqDeallocateContext(struct TSQCompressionContext* ctx)
{
    if (ctx->refhash) align_free(ctx->refhash);
//    if (ctx->refhash) free(ctx->refhash);
    free(ctx);
}


extern "C" struct TSQCompressionContext* tsqAllocateContext()
{
    struct TSQCompressionContext* context = (struct TSQCompressionContext*) malloc( sizeof(struct TSQCompressionContext) );

    if (context)
    {
        context->refhash = nullptr;
        //context->refhash = (uint16_t*) malloc( TSQ_HASH_SZ );
        context->refhash = (uint16_t*) align_alloc( MAX_CACHE_LINE_SIZE, TSQ_HASH_SZ );

        if (!context->refhash)
        {
            tsqDeallocateContext(context);
            context = nullptr;
        }
    }

    return context;
}


extern void compression_read_worker( TSQCompressionContext_MT* ctx );
extern void compression_worker( uint32_t threadid, TSQCompressionContext_MT* ctx );
extern void compression_write_worker( TSQCompressionContext_MT* ctx );



extern "C" struct TSQCompressionContext_MT* tsqAllocateContextCompression_MT( bool verbose )
{
    uint32_t num_cores = std::thread::hardware_concurrency();
    if (num_cores == 0) num_cores = 1;

    TSQCompressionContext_MT* ctx = new TSQCompressionContext_MT();
    if (!ctx) return nullptr;

    ctx->num_cores = num_cores;
    ctx->workers = new TSQWorker[num_cores];

    for (uint32_t i = 0; i < num_cores; ++i) {
        ctx->workers[i].n_inputs = 3;
        ctx->workers[i].n_outputs = 3;
        ctx->workers[i].currentReadInput = 0;
        ctx->workers[i].currentWorkInput = 0;
        ctx->workers[i].currentWorkOutput = 0;
        ctx->workers[i].currentWriteOutput = 0;

        // Allocate input buffers
        for (uint32_t j = 0; j < ctx->workers[i].n_inputs; ++j) {
            TSQBuffer buffer;
            buffer.buffer = nullptr;
            buffer.filebuffer = (uint8_t*) malloc( TSQ_BLOCK_SZ * sizeof(uint8_t));
            buffer.size = TSQ_BLOCK_SZ;
            ctx->workers[i].inputs.push_back(buffer);
        }

        // Allocate output buffers
        for (uint32_t j = 0; j < ctx->workers[i].n_outputs; ++j) {
            TSQBuffer buffer;
            buffer.buffer = nullptr;
            buffer.filebuffer = (uint8_t*) malloc(TSQ_OUTPUT_SZ * sizeof(uint8_t));
            buffer.size = TSQ_OUTPUT_SZ;
            ctx->workers[i].outputs.push_back(buffer);
        }
    }

    // no job in progress
    ctx->input_blocks = 0;
    ctx->queue = new std::queue<TSQJob*>();
    ctx->maxjobid = 1;
    ctx->verbose = verbose;

    // initialize threads and job queue
    ctx->exit_request = false;
    ctx->threads = new std::thread*[num_cores];
    ctx->reader = new std::thread( compression_read_worker, ctx );
    ctx->writer = new std::thread( compression_write_worker, ctx );
    for (uint32_t i = 0; i < num_cores; ++i) {
        ctx->threads[i] = new std::thread( compression_worker, i, ctx );
    }

    return ctx;
}

extern "C" void tsqDeallocateContextCompression_MT(TSQCompressionContext_MT* ctx)
{
    if (!ctx) return;

    // Wait for job queue to be empty
    {
        std::unique_lock<std::mutex> lock(ctx->req_mtx);
        ctx->req_cv.wait(lock, [ctx] {
            return ctx->inflight_reqs == 0;
        });
    }

    // Signal threads to exit
    ctx->exit_request = true;
    ctx->queue_cv.notify_all();
    ctx->reader_cv.notify_all();

    // Join threads
    if (ctx->reader) {
        ctx->reader->join();
        delete ctx->reader;
    }
    if (ctx->threads) {
        for (uint32_t i = 0; i < ctx->num_cores; ++i) {
            if (ctx->threads[i]) {
                ctx->workers[i].input_cv.notify_all();
                ctx->workers[i].output_cv.notify_all();
                ctx->threads[i]->join();
                delete ctx->threads[i];
            }
        }
        delete[] ctx->threads;
    }
    if (ctx->writer) {
        ctx->writer->join();
        delete ctx->writer;
    }

    // Free buffers
    if (ctx->workers) {
        for (uint32_t i = 0; i < ctx->num_cores; ++i) {
            for (auto& input : ctx->workers[i].inputs) {
                if (input.filebuffer) free(input.filebuffer);
            }
            ctx->workers[i].inputs.clear();
            for (auto& output : ctx->workers[i].outputs) {
                if (output.filebuffer) free(output.filebuffer);
            }
            ctx->workers[i].outputs.clear();
        }
        delete[] ctx->workers;
    }

    // Free queue
    delete ctx->queue;
    delete ctx;
}


extern void decompression_read_worker( TSQDecompressionContext_MT* ctx );
extern void decompression_worker( uint32_t threadid, TSQDecompressionContext_MT* ctx );
extern void decompression_write_worker( TSQDecompressionContext_MT* ctx );


extern "C" struct TSQDecompressionContext_MT* tsqAllocateContextDecompression_MT( bool verbose )
{
    uint32_t num_cores = std::thread::hardware_concurrency();
    if (num_cores == 0) num_cores = 1;

    TSQDecompressionContext_MT* ctx = new TSQDecompressionContext_MT();
    if (!ctx) return nullptr;

    ctx->num_cores = num_cores;
    ctx->workers = new TSQWorker[num_cores];

    for (uint32_t i = 0; i < num_cores; ++i) {
        ctx->workers[i].n_inputs = 3;
        ctx->workers[i].n_outputs = 3;
        ctx->workers[i].currentReadInput = 0;
        ctx->workers[i].currentWorkInput = 0;
        ctx->workers[i].currentWorkOutput = 0;
        ctx->workers[i].currentWriteOutput = 0;

        // Allocate input buffers
        for (uint32_t j = 0; j < ctx->workers[i].n_inputs; ++j) {
            TSQBuffer buffer;
            buffer.buffer = nullptr;
            buffer.filebuffer = (uint8_t*) malloc(65536+TSQ_OUTPUT_SZ * sizeof(uint8_t));
            memset( buffer.filebuffer, 0, 65536 );
            buffer.size = TSQ_OUTPUT_SZ;
            ctx->workers[i].inputs.push_back(buffer);
        }

        // Allocate output buffers
        for (uint32_t j = 0; j < ctx->workers[i].n_outputs; ++j) {
            TSQBuffer buffer;
            buffer.buffer = nullptr;
            buffer.filebuffer = (uint8_t*) malloc(256+TSQ_BLOCK_SZ * sizeof(uint8_t));
            buffer.size = TSQ_BLOCK_SZ;
            ctx->workers[i].outputs.push_back(buffer);
        }
    }

    // no job in progress
    ctx->input_blocks = 0;
    ctx->queue = new std::queue<TSQJob*>();
    ctx->maxjobid = 1;
    ctx->verbose = verbose;

    // initialize threads and job queue
    ctx->exit_request = false;
    ctx->threads = new std::thread*[num_cores];
    for (uint32_t i = 0; i < num_cores; ++i) {
        ctx->threads[i] = new std::thread( decompression_worker, i, ctx );
    }
    ctx->reader = new std::thread( decompression_read_worker, ctx );
    ctx->writer = new std::thread( decompression_write_worker, ctx );

    return ctx;
}

extern "C" void tsqDeallocateContextDecompression_MT(struct TSQDecompressionContext_MT* ctx)
{
    if (!ctx) return;

    // Wait for job queue to be empty
    {
        std::unique_lock<std::mutex> lock(ctx->req_mtx);
        ctx->req_cv.wait(lock, [ctx] {
            return ctx->inflight_reqs == 0;
        });
    }

    // Signal threads to exit
    ctx->exit_request = true;
    ctx->queue_cv.notify_all();
    ctx->reader_cv.notify_all();

    // Join threads
    if (ctx->reader) {
        ctx->reader->join();
        delete ctx->reader;
    }
    if (ctx->threads) {
        for (uint32_t i = 0; i < ctx->num_cores; ++i) {
            if (ctx->threads[i]) {
                ctx->workers[i].input_cv.notify_all();
                ctx->workers[i].output_cv.notify_all();
                ctx->threads[i]->join();
                delete ctx->threads[i];
            }
        }
        delete[] ctx->threads;
    }
    if (ctx->writer) {
        ctx->writer->join();
        delete ctx->writer;
    }

    // Free buffers
    if (ctx->workers) {
        for (uint32_t i = 0; i < ctx->num_cores; ++i) {
            for (auto& input : ctx->workers[i].inputs) {
                if (input.filebuffer) free(input.filebuffer);
            }
            ctx->workers[i].inputs.clear();
            for (auto& output : ctx->workers[i].outputs) {
                if (output.filebuffer) free(output.filebuffer);
            }
            ctx->workers[i].outputs.clear();
        }
        delete[] ctx->workers;
    }

    delete ctx->queue;
    delete ctx;
}
