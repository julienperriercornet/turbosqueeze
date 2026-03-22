
/*
 * Turbosqueeze thread functions.
 *
 * Multi-threaded worker and job queue logic for high-performance compression and decompression.
 * Implements producer-consumer patterns, worker synchronization, and asynchronous job handling.
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


static inline bool worker_has_input_capacity(const TSQWorker& worker)
{
    const uint64_t current_read_input = worker.currentReadInput.load();
    const uint64_t current_work_input = worker.currentWorkInput.load();
    return (current_read_input >= current_work_input) &&
        ((current_read_input - current_work_input) < worker.n_inputs);
}

static inline bool worker_has_pending_input(const TSQWorker& worker)
{
    return worker.currentReadInput.load() > worker.currentWorkInput.load();
}

static inline bool worker_has_output_capacity(const TSQWorker& worker)
{
    const uint64_t current_work_output = worker.currentWorkOutput.load();
    const uint64_t current_write_output = worker.currentWriteOutput.load();
    return (current_work_output - current_write_output) < worker.n_outputs;
}

static inline bool worker_has_pending_output(const TSQWorker& worker)
{
    return worker.currentWorkOutput.load() > worker.currentWriteOutput.load();
}

static inline bool exit_requested(const TSQCompressionContext_MT* ctx)
{
    return ctx->exit_request.load();
}

static inline bool exit_requested(const TSQDecompressionContext_MT* ctx)
{
    return ctx->exit_request.load();
}


void compression_read_worker( TSQCompressionContext_MT* ctx )
{
    while (true)
    {
        TSQJob* job = nullptr;
        {
            std::unique_lock<std::mutex> lock(ctx->queue_mtx);
            ctx->queue_cv.wait(lock, [&]{ return (!ctx->queue->empty()) || exit_requested(ctx); });
            if (exit_requested(ctx))
                break;

            job = ctx->queue->front();
            ctx->queue->pop();
        }

        FILE* input_stream = job->input_stream;
        uint8_t* input_buffer = job->input;
        size_t input_size = job->input_size;
        uint64_t start_block = job->start_block;
        uint64_t end_block = start_block + job->n_blocks;
        bool use_extensions = job->use_extensions;
        uint32_t compression_level = job->compression_level;

        for (uint64_t i = start_block; i < end_block; i++)
        {
            uint32_t curworker = i % ctx->num_cores;

            {
                std::unique_lock<std::mutex> lock(ctx->reader_mtx);
                if (!worker_has_input_capacity(ctx->workers[curworker]))
                {
                    ctx->reader_cv.wait(lock, [curworker, ctx]{ return worker_has_input_capacity(ctx->workers[curworker]); });
                }
            }

            uint32_t curbuf = ctx->workers[curworker].currentReadInput.load() % ctx->workers[curworker].n_inputs;
            uint32_t to_read = std::min( (size_t) TSQ_BLOCK_SZ, input_size - (i - start_block) * TSQ_BLOCK_SZ);

            ctx->workers[curworker].inputs[curbuf].job = job;
            ctx->workers[curworker].inputs[curbuf].version = job->compression_method;

            if (to_read>0 && to_read<=TSQ_BLOCK_SZ)
            {
                if (input_stream)
                {
                    uint8_t *inbuff = ctx->workers[curworker].inputs[curbuf].filebuffer;
                    size_t actually_read = fread(inbuff, 1, to_read, input_stream);

                    if (actually_read == to_read)
                    {
                        ctx->workers[curworker].inputs[curbuf].buffer = inbuff;
                        ctx->workers[curworker].inputs[curbuf].size = to_read;
                        ctx->workers[curworker].inputs[curbuf].ext = use_extensions;
                        ctx->workers[curworker].inputs[curbuf].compression_level = compression_level;
                    }
                    else
                    {
                        // Propagate the error down the pipeline
                        ctx->workers[curworker].inputs[curbuf].buffer = nullptr;
                        ctx->workers[curworker].inputs[curbuf].size = 0;
                    }
                }
                else
                {
                    ctx->workers[curworker].inputs[curbuf].buffer = input_buffer + (i - start_block) * TSQ_BLOCK_SZ;
                    ctx->workers[curworker].inputs[curbuf].size = to_read;
                    ctx->workers[curworker].inputs[curbuf].ext = use_extensions;
                    ctx->workers[curworker].inputs[curbuf].compression_level = compression_level;
                }

                // Signal the worker thread that one input buffer is ready
                {
                    std::lock_guard<std::mutex> lock(ctx->workers[curworker].input_mtx);
                    ctx->workers[curworker].currentReadInput.fetch_add(1);
                }
                ctx->workers[curworker].input_cv.notify_one();
            }
            else
            {
                // We chose to propagate the error down the pipeline in case of an error
                ctx->workers[curworker].inputs[curbuf].buffer = nullptr;
                ctx->workers[curworker].inputs[curbuf].size = 0;
                {
                    std::lock_guard<std::mutex> lock(ctx->workers[curworker].input_mtx);
                    ctx->workers[curworker].currentReadInput.fetch_add(1);
                }
                ctx->workers[curworker].input_cv.notify_one();
            }
        }
    }
}


static void lazy_init_compressctx(struct TSQCompressionContext** compressctx, struct TSQCompressionContextHist** histctx, TSQOptContext** optctx, uint32_t compression_version, uint32_t compression_level)
{
    if (!*compressctx && compression_level == 0) *compressctx = tsqAllocateContext(); // same context for v1 and v2
    if (!*histctx && compression_version == 2 && compression_level != 0 && compression_level <= 5) *histctx = tsqAllocateContextHist( 5 );
    if (!*optctx && compression_version == 2 && compression_level == 6) *optctx = tsqAllocateContextOpt();
}


void compression_worker( uint32_t threadid, TSQCompressionContext_MT* ctx )
{
    TSQWorker& worker = ctx->workers[threadid];

    struct TSQCompressionContext* fastctx = nullptr;
    struct TSQCompressionContextHist* histctx = nullptr;
    TSQOptContext* optctx = nullptr;

    while (true)
    {
        {
            std::unique_lock<std::mutex> lock(worker.input_mtx);
            if (!(worker_has_pending_input(worker) || exit_requested(ctx)))
            {
                worker.input_cv.wait(lock, [&worker,ctx]{ return worker_has_pending_input(worker) || exit_requested(ctx); });
            }
        }

        if (exit_requested(ctx)) break;

        uint32_t curin = worker.currentWorkInput.load() % worker.n_inputs;

        {
            std::unique_lock<std::mutex> lock(worker.output_mtx);
            if (!(worker_has_output_capacity(worker) || exit_requested(ctx)))
            {
                worker.output_cv.wait(lock, [&worker,ctx]{ return worker_has_output_capacity(worker) || exit_requested(ctx); });
            }
        }

        if (exit_requested(ctx)) break;

        uint32_t curout = worker.currentWorkOutput.load() % worker.n_outputs;

        assert(worker.inputs[curin].job != nullptr);

        uint8_t* inbuff = worker.inputs[curin].buffer;
        uint8_t *outbuff = worker.outputs[curout].filebuffer;

        worker.outputs[curout].size = 0;
        worker.outputs[curout].job = worker.inputs[curin].job;
        worker.outputs[curout].ext = worker.inputs[curin].ext;
        worker.outputs[curout].version = worker.inputs[curin].version;
        worker.outputs[curout].compression_level = worker.inputs[curin].compression_level;

        assert( worker.currentWorkInput.load() == worker.currentWorkOutput.load() );

        // Compression logic
        if (inbuff != nullptr)
        {
            lazy_init_compressctx(&fastctx, &histctx, &optctx, worker.inputs[curin].version, worker.inputs[curin].compression_level);

            if (worker.inputs[curin].compression_level == 0)
            {
                tsqInit(fastctx);
                if (worker.inputs[curin].version == 1) tsqEncode(fastctx, inbuff, outbuff, &worker.outputs[curout].size, worker.inputs[curin].size, worker.inputs[curin].ext);
                else if (worker.inputs[curin].version == 2) tsqEncode2_fast(fastctx, inbuff, outbuff, &worker.outputs[curout].size, worker.inputs[curin].size);
            }
            else if (worker.inputs[curin].compression_level >= 1 && worker.inputs[curin].compression_level <= 5)
            {
                tsqInitHist(histctx);
                tsqEncode2_hist(histctx, inbuff, outbuff, &worker.outputs[curout].size, worker.inputs[curin].size);
            }
            else if (worker.inputs[curin].compression_level == 6)
            {
                tsqEncode2_opt(optctx, inbuff, outbuff, &worker.outputs[curout].size, worker.inputs[curin].size);
            }
            else
            {
                // Invalid compression level, we skip processing this block and all succeeding blocks
                worker.outputs[curout].size = 0;
            }
        }

        {
            std::lock_guard<std::mutex> lock(ctx->reader_mtx);
            worker.currentWorkInput.fetch_add(1);
        }
        ctx->reader_cv.notify_one();

        {
            std::lock_guard<std::mutex> lock(worker.output_mtx);
            worker.currentWorkOutput.fetch_add(1);
        }
        worker.output_cv.notify_one();
    }

    // Cleanup after the worker thread is done
    if (fastctx) tsqDeallocateContext(fastctx);
    if (histctx) tsqDeallocateContextHist(histctx);
    if (optctx) tsqDeallocateContextOpt(optctx);
}


void compression_write_worker( TSQCompressionContext_MT* ctx )
{
    uint32_t num_cores = ctx->num_cores;
    uint64_t i = 0;

    while (true)
    {
        uint32_t threadid = i % num_cores;
        TSQWorker& worker = ctx->workers[threadid];

        {
            std::unique_lock<std::mutex> lock(worker.output_mtx);

            if (!(worker_has_pending_output(worker) || exit_requested(ctx)))
            {
                // Wait until there is something to write from this worker
                worker.output_cv.wait(lock, [&worker,ctx]{
                    return worker_has_pending_output(worker) || exit_requested(ctx);
                });
            }
        }

        if (exit_requested(ctx)) break;

        uint32_t curout = worker.currentWriteOutput.load() % worker.n_outputs;
        TSQJob* job = worker.outputs[curout].job;
        assert( job != nullptr );
        uint8_t* outbuff = worker.outputs[curout].filebuffer;
        uint32_t outsize = worker.outputs[curout].size;
        uint32_t outmask = outsize;
        if (worker.outputs[curout].ext) outmask |= 0x800000;

        if (outsize!=0 && !job->error_occurred)
        {
            // Write the size header (3 bytes, as in compress())
            if (job->output_file)
            {
                fputc(outmask & 0xFF, job->output_stream);
                fputc((outmask >> 8) & 0xFF, job->output_stream);
                fputc((outmask >> 16) & 0xFF, job->output_stream);
                fwrite(outbuff, 1, outsize, job->output_stream);
                job->outsize += 3 + outsize;
            }
            else
            {
                job->output[0] = outmask & 0xFF;
                job->output[1] = (outmask >> 8) & 0xFF;
                job->output[2] = (outmask >> 16) & 0xFF;
                memcpy(job->output + 3, outbuff, outsize);
                job->output += outsize + 3; // Move output pointer past the written data 
                job->outsize += 3 + outsize;
            }
        }
        else
        {
            // An error occurred during processing, we skip writing this block and all succeeding blocks
            job->error_occurred |= true;
        }

        if (job->progress_cb && job->n_blocks)
        {
            double progress = double(i + 1 - job->start_block) / double(job->n_blocks);
            if (progress < 0.0) progress = 0.0;
            if (progress > 1.0) progress = 1.0;
            job->progress_cb(job->jobid, progress);
        }

        if (i == job->start_block + job->n_blocks - 1)
        {
            // Job is complete
            if (job->completion_cb)
            {
                job->completion_cb(job->jobid, !job->error_occurred); // Notify completion
            }
            {
                std::lock_guard<std::mutex> lock(ctx->req_mtx);
                ctx->inflight_reqs.fetch_sub(1);
            }
            ctx->req_cv.notify_all();
            delete job;
        }

        {
            std::lock_guard<std::mutex> lock(worker.output_mtx);
            worker.currentWriteOutput.fetch_add(1);
        }
        worker.output_cv.notify_one();

        i++;
    }
}


extern "C" uint32_t tsqCompressAsync_MT( TSQCompressionContext_MT* ctx, uint8_t* in, size_t szin, bool infile, uint8_t** out, size_t *szout, bool outfile, uint32_t version, uint32_t level,
    std::function<void(uint32_t jobid, bool)> user_completion_cb, std::function<void(uint32_t jobid, double)> user_progress_cb )
{
    uint32_t jobid;
    const uint32_t compression_method = version;
    const uint32_t stream_header_size = compression_method == 2 ? 19 : 16;

    uint8_t* pp = nullptr;
    TSQJob *job = new TSQJob();

    if (!job) return 0;

    job->input = in;
    job->input_size = job->size = szin;
    job->input_file = infile;

    if (infile)
    {
        job->input_stream = fopen((const char*) job->input, "rb");

        if (!job->input_stream)
        {
            if (ctx->verbose)
            {
                printf("Error: could not open input file.\n");
            }
            if (user_completion_cb)
                user_completion_cb(0, false);
            delete job;
            return 0;
        }

        fseek(job->input_stream,0,SEEK_END);
        job->input_size = ftell(job->input_stream);
        fseek(job->input_stream,0,SEEK_SET);
    }

    job->n_blocks = (job->input_size % TSQ_BLOCK_SZ != 0 ? 1 : 0) + (job->input_size / TSQ_BLOCK_SZ);

    if (outfile)
    {
        job->output = nullptr;
        job->outsize = 0;
        job->output_stream = fopen((const char*) *out, "wb");

        if (!job->output_stream)
        {
            if (ctx->verbose)
            {
                printf("Error: could not open output file.\n");
            }
            if (user_completion_cb)
                user_completion_cb(0, false);
            delete job;
            return 0;
        }

        fwrite(compression_method == 2 ? "TSQ2" : "TSQ1", 1, 4, job->output_stream);
        fwrite(&job->n_blocks, 1, 4, job->output_stream);
        fwrite(&job->input_size, 1, sizeof(uint64_t), job->output_stream);
        if (compression_method == 2)
        {
            fputc(compression_method & 0xFF, job->output_stream);
            fputc((compression_method >> 8) & 0xFF, job->output_stream);
            fputc((compression_method >> 16) & 0xFF, job->output_stream);
        }
    }
    else
    {
        size_t alloc_size = stream_header_size + (size_t) job->n_blocks * (TSQ_OUTPUT_SZ + 3);
        job->output = (uint8_t*) malloc( alloc_size );

        if (!job->output)
        {
            if (ctx->verbose)
            {
                printf("Error: could not allocate output buffer.\n");
            }
            if (user_completion_cb)
                user_completion_cb(0, false);
            delete job;
            return 0;
        }

        pp = job->output;
        job->outsize = 0;
        memcpy(job->output, compression_method == 2 ? "TSQ2" : "TSQ1", 4);
        memcpy(job->output + 4, &job->n_blocks, 4);
        memcpy(job->output + 8, &job->input_size, sizeof(uint64_t));
        if (compression_method == 2)
        {
            job->output[16] = compression_method & 0xFF;
            job->output[17] = (compression_method >> 8) & 0xFF;
            job->output[18] = (compression_method >> 16) & 0xFF;
        }
        job->output += stream_header_size; // Move output pointer past the header
        job->outsize += stream_header_size;
    }

    job->output_file = outfile;
    job->compression_method = version;
    job->compression_level = level;
    job->use_extensions = false;

    job->completion_cb = [user_completion_cb,ctx,job,out,szout,pp](uint32_t jobid, bool success) {
        if (ctx->verbose)
        {
            if (success) {
                printf("Job %u completed successfully.\n", jobid);
            } else {
                printf("Job %u failed.                \n", jobid);
            }
        }
        if (!job->output_file)
        {
            *out = pp;
            *szout = job->outsize;
        }
        if (user_completion_cb)
        {
            user_completion_cb(jobid, success);
        }
    };
    job->progress_cb = [user_progress_cb,ctx](uint32_t jobid, double progress) {
        if (ctx->verbose)
        {
            printf("Job %u progress: %.2f%%\r", jobid, progress * 100.0);
        }
        if (user_progress_cb)
            user_progress_cb(jobid, progress);
    };

    {
        std::lock_guard<std::mutex> lock(ctx->req_mtx);
        ctx->inflight_reqs.fetch_add(1);
    }
    ctx->req_cv.notify_all();

    ctx->queue_mtx.lock();
    jobid = job->jobid = ctx->maxjobid++;
    job->start_block = ctx->input_blocks;
    ctx->input_blocks += job->n_blocks;
    ctx->queue->push(job);
    ctx->queue_mtx.unlock();
    ctx->queue_cv.notify_all();

    return jobid;
}


extern "C" bool tsqCompress_MT( TSQCompressionContext_MT* ctx, uint8_t* in, size_t szin, bool infile, uint8_t** out, size_t* szout, bool outfile, uint32_t version, uint32_t level )
{
    if (!ctx || !in || szin == 0 || !out || szout == 0)
    {
        return false; // Invalid parameters
    }

    std::mutex completion_mtx;
    std::condition_variable completion_cv;
    bool finished = false;
    bool return_status;

    tsqCompressAsync_MT( ctx, in, szin, infile, out, szout, outfile, version, level,
        [&finished,&return_status,&completion_cv](uint32_t jobid, bool success) {
            finished = true;
            return_status = success;
            completion_cv.notify_one();
        },
        nullptr
    );

    // We block until job completion
    {
        std::unique_lock<std::mutex> lock(completion_mtx);
        completion_cv.wait(lock, [&finished]{ return finished; });
    }

    return return_status; // Return the status of the compression job
}


void decompression_read_worker( TSQDecompressionContext_MT* ctx )
{
    while (true)
    {
        TSQJob* job = nullptr;
        {
            std::unique_lock<std::mutex> lock(ctx->queue_mtx);
            ctx->queue_cv.wait(lock, [&]{ return !ctx->queue->empty() || exit_requested(ctx); });
            if (exit_requested(ctx))
                break;

            job = ctx->queue->front();
            ctx->queue->pop();
        }

        FILE* input_stream = job->input_stream;
        size_t input_size = job->input_size;
        uint32_t start_block = job->start_block;
        uint32_t end_block = start_block + job->n_blocks;

        for (uint32_t i = start_block; i < end_block; i++)
        {
            uint32_t curworker = i % ctx->num_cores;

            {
                std::unique_lock<std::mutex> lock(ctx->reader_mtx);
                if (!worker_has_input_capacity(ctx->workers[curworker]))
                {
                    ctx->reader_cv.wait(lock, [curworker, ctx]{ return worker_has_input_capacity(ctx->workers[curworker]); });
                }
            }

            uint32_t to_read;
            uint32_t with_extensions;
            uint32_t curbuf = ctx->workers[curworker].currentReadInput.load() % ctx->workers[curworker].n_inputs;
            ctx->workers[curworker].inputs[curbuf].job = job;
            ctx->workers[curworker].inputs[curbuf].version = i;

            if (input_stream)
            {
                to_read = fgetc( input_stream );
                to_read |= fgetc( input_stream ) << 8;
                to_read |= fgetc( input_stream ) << 16;
                with_extensions = (to_read & 0x800000) != 0;
                to_read &= 0x7FFFFF;

                if (to_read>0 && to_read<=TSQ_OUTPUT_SZ)
                {
                    uint8_t* inbuff = ctx->workers[curworker].inputs[curbuf].filebuffer;
                    uint32_t actually_read = fread(inbuff, 1, to_read, input_stream);

                    if (actually_read == to_read)
                    {
                        ctx->workers[curworker].inputs[curbuf].buffer = inbuff;
                        ctx->workers[curworker].inputs[curbuf].size = to_read;
                        ctx->workers[curworker].inputs[curbuf].ext = with_extensions;
                    }
                    else
                    {
                        // We chose to propagate the error down the pipeline in case of an error
                        ctx->workers[curworker].inputs[curbuf].buffer = nullptr;
                        ctx->workers[curworker].inputs[curbuf].size = 0;
                    }
                }
                else
                {
                    // We chose to propagate the error down the pipeline in case of an error
                    ctx->workers[curworker].inputs[curbuf].buffer = nullptr;
                    ctx->workers[curworker].inputs[curbuf].size = 0;
                }
            }
            else
            {
                to_read = job->input[0];
                to_read |= job->input[1] << 8;
                to_read |= job->input[2] << 16;
                with_extensions = (to_read & 0x800000) != 0;
                to_read &= 0x7FFFFF;

                if (to_read > 0 && to_read<=TSQ_OUTPUT_SZ)
                {
                    ctx->workers[curworker].inputs[curbuf].buffer = job->input + 3;
                    ctx->workers[curworker].inputs[curbuf].size = to_read;
                    ctx->workers[curworker].inputs[curbuf].ext = with_extensions;
                    job->input += to_read + 3;
                }
                else
                {
                    // We chose to propagate the error down the pipeline in case of an error
                    ctx->workers[curworker].inputs[curbuf].buffer = nullptr;
                    ctx->workers[curworker].inputs[curbuf].size = 0;
                }
            }

            {
                std::lock_guard<std::mutex> lock(ctx->workers[curworker].input_mtx);
                ctx->workers[curworker].currentReadInput.fetch_add(1);
            }
            ctx->workers[curworker].input_cv.notify_one();
        }
    }
}


void decompression_worker( uint32_t threadid, TSQDecompressionContext_MT* ctx )
{
    uint64_t i = 0;

    while (true)
    {
        {
            std::unique_lock<std::mutex> lock(ctx->workers[threadid].input_mtx);
            if (!(worker_has_pending_input(ctx->workers[threadid]) || exit_requested(ctx)))
            {
                ctx->workers[threadid].input_cv.wait(lock, [ctx,threadid]{ return worker_has_pending_input(ctx->workers[threadid]) || exit_requested(ctx); });
            }
        }

        if (exit_requested(ctx)) break;

        uint32_t curbuf = ctx->workers[threadid].currentWorkInput.load() % ctx->workers[threadid].n_inputs;

        {
            std::unique_lock<std::mutex> lock(ctx->workers[threadid].output_mtx);
            if (!(worker_has_output_capacity(ctx->workers[threadid]) || exit_requested(ctx)))
            {
                ctx->workers[threadid].output_cv.wait(lock, [ctx,threadid]{ return worker_has_output_capacity(ctx->workers[threadid]) || exit_requested(ctx); });
            }
        }

        if (exit_requested(ctx)) break;

        TSQJob* job = ctx->workers[threadid].inputs[curbuf].job;

        uint32_t curout = ctx->workers[threadid].currentWorkOutput.load() % ctx->workers[threadid].n_outputs;

        uint8_t* inbuff = ctx->workers[threadid].inputs[curbuf].buffer;
        uint8_t* outbuff = ctx->workers[threadid].outputs[curout].filebuffer;

        if (!job->output_file && job->compression_method == 2)
        {
            const uint64_t block_index = ctx->workers[threadid].inputs[curbuf].version;
            outbuff = job->output + (block_index - job->start_block) * TSQ_BLOCK_SZ;
        }

        ctx->workers[threadid].outputs[curout].job = ctx->workers[threadid].inputs[curbuf].job;
        ctx->workers[threadid].outputs[curout].size = 0;
        ctx->workers[threadid].outputs[curout].version = ctx->workers[threadid].inputs[curbuf].version;

        assert( ctx->workers[threadid].currentWorkInput.load() == ctx->workers[threadid].currentWorkOutput.load() );

        // Decompression logic
        if (inbuff != nullptr)
        {
            if (job->compression_method == 2)
            {
                tsqDecode2( inbuff, outbuff, &ctx->workers[threadid].outputs[curout].size, ctx->workers[threadid].inputs[curbuf].size );
            }
            else
            {
                tsqDecode( inbuff, outbuff, &ctx->workers[threadid].outputs[curout].size, ctx->workers[threadid].inputs[curbuf].size, ctx->workers[threadid].inputs[curbuf].ext );
            }
        }

        {
            std::lock_guard<std::mutex> lock(ctx->reader_mtx);
            ctx->workers[threadid].currentWorkInput.fetch_add(1);
        }
        ctx->reader_cv.notify_one();

        {
            std::lock_guard<std::mutex> lock(ctx->workers[threadid].output_mtx);
            ctx->workers[threadid].currentWorkOutput.fetch_add(1);
        }
        ctx->workers[threadid].output_cv.notify_one();
    }
}


void decompression_write_worker( TSQDecompressionContext_MT* ctx )
{
    uint32_t num_cores = ctx->num_cores;
    uint64_t i = 0;

    while (true)
    {
        uint32_t threadid = i % num_cores;
        TSQWorker& worker = ctx->workers[threadid];

        {
            std::unique_lock<std::mutex> lock(worker.output_mtx);
            if (!(worker_has_pending_output(worker) || exit_requested(ctx)))
            {
                // Wait until there is something to write from this worker
                worker.output_cv.wait(lock, [&worker,ctx]{
                    return worker_has_pending_output(worker) || exit_requested(ctx);
                });
            }
        }

        if (exit_requested(ctx)) break;

        uint32_t curout = worker.currentWriteOutput.load() % worker.n_outputs;
        TSQJob* job = worker.outputs[curout].job;
        uint8_t* outbuff = worker.outputs[curout].filebuffer;
        uint32_t outsize = worker.outputs[curout].size;

        if (outsize==0)
        {
            // An error occurred during processing, we skip writing this block and all succeeding blocks
            job->error_occurred |= true;
        }

        if (!job->error_occurred)
        {
            // Write the decompressed data
            if (job->output_file)
            {
                fwrite(outbuff, 1, outsize, job->output_stream);
                job->outsize += outsize;
            }
            else
            {
                if (job->compression_method == 1)
                {
                    memcpy(job->output, outbuff, outsize);
                }
                job->outsize += outsize;
            }
        }

        if (job->progress_cb && job->n_blocks)
            job->progress_cb(job->jobid, double(i-job->start_block+1) / double(job->n_blocks) );

        if (i == job->start_block + job->n_blocks - 1)
        {
            // Job is complete
            if (job->completion_cb)
            {
                job->completion_cb(job->jobid, !job->error_occurred); // Notify completion
            }
            {
                std::lock_guard<std::mutex> lock(ctx->req_mtx);
                ctx->inflight_reqs.fetch_sub(1);
            }
            ctx->req_cv.notify_all();
            delete job;
        }

        {
            std::lock_guard<std::mutex> lock(worker.output_mtx);
            worker.currentWriteOutput.fetch_add(1);
        }
        worker.output_cv.notify_one();

        i++;
    }
}


extern "C" uint32_t tsqDecompressAsync_MT( TSQDecompressionContext_MT* ctx, uint8_t* in, size_t szin, bool infile, uint8_t** out, size_t* szout, bool outfile,
    std::function<void(uint32_t jobid, bool)> user_completion_cb, std::function<void(uint32_t jobid, double)> user_progress_cb )
{
    uint32_t jobid = 0;
    uint8_t* pp = nullptr;
    TSQJob *job = new TSQJob;

    if (!job)
    {
        if (user_completion_cb)
            user_completion_cb(0, false);
        return 0;
    }

    job->input = in;
    job->size = szin;
    job->input_file = infile;

    char magic[5];
    magic[4] = 0;
    char magic_key1[5] = "TSQ1";
    char magic_key2[5] = "TSQ2";

    uint32_t n_blocks = 0;
    uint32_t compression_method = 1;

    if (infile)
    {
        job->input_stream = fopen((const char*) job->input, "rb");

        if (!job->input_stream)
        {
            if (ctx->verbose)
            {
                printf("Error opening input file: %s\n", job->input);
            }
            if (user_completion_cb)
                user_completion_cb(0, false);
            delete job;
            return 0;
        }

        fseek(job->input_stream,0,SEEK_END);
        job->input_size = ftell(job->input_stream);
        fseek(job->input_stream,0,SEEK_SET);

        uint32_t read_magic = fread(&magic[0], 1, 4, job->input_stream);
        uint32_t read_block = fread(&n_blocks, 1, 4, job->input_stream);
        uint32_t read_is = fread(&job->outsize, 1, sizeof(uint64_t), job->input_stream);

        if (strncmp(&magic[0], &magic_key1[0], 4) == 0)
        {
            compression_method = 1;
        }
        else if (strncmp(&magic[0], &magic_key2[0], 4) == 0)
        {
            compression_method = 2;

            uint32_t method_l = fgetc(job->input_stream);
            method_l |= fgetc(job->input_stream) << 8;
            method_l |= fgetc(job->input_stream) << 16;
            compression_method = method_l;
        }
        else
        {
            if (ctx->verbose)
            {
                printf("Error: signature mismatch (%s but expected %s or %s).\n", &magic[0], &magic_key1[0], &magic_key2[0]);
            }
            if (user_completion_cb)
                user_completion_cb(0, false);
            delete job;
            return 0;
        }
    }
    else
    {
        if (memcmp(job->input, &magic_key1[0], 4) == 0)
        {
            compression_method = 1;
        }
        else if (memcmp(job->input, &magic_key2[0], 4) == 0)
        {
            compression_method = 2;
        }
        else
        {
            if (user_completion_cb)
                user_completion_cb(0, false);
            delete job;
            return 0;
        }

        memcpy(&n_blocks, job->input + 4, 4);
        memcpy(&job->outsize, job->input + 8, sizeof(uint64_t));

        if (compression_method == 2)
        {
            uint32_t method_l = job->input[16];
            method_l |= job->input[17] << 8;
            method_l |= job->input[18] << 16;
            compression_method = method_l;
            job->input += 19; // Move input pointer past TSQ2 header
        }
        else
        {
            job->input += 16; // Move input pointer past TSQ1 header
        }
    }

    if (n_blocks == 0)
    {
        if (ctx->verbose)
        {
            printf("Error: no blocks to decode in input file.\n");
        }
        job->completion_cb(0, false);
        delete job;
        return 0; // No blocks to process
    }

    job->n_blocks = n_blocks;
    job->error_occurred = false;
    job->output_file = outfile;
    job->compression_method = compression_method;

    if (outfile)
    {
        job->output_stream = fopen((const char*) *out, "wb");

        if (!job->output_stream)
        {
            if (ctx->verbose)
            {
                printf("Error opening output file: %s\n", job->output);
            }
            if (user_completion_cb)
                user_completion_cb(0, false);
            delete job;
            return 0;
        }

        job->output = nullptr;
        job->outsize = 0;
    }
    else
    {
        job->output = (uint8_t*) malloc( job->outsize+MAX_CACHE_LINE_SIZE );
        pp = job->output;

        if (!job->output)
        {
            if (ctx->verbose)
            {
                printf("Error allocating output buffer of size %zu bytes.\n", job->outsize+32);
            }
            if (user_completion_cb)
                user_completion_cb(0, false);
            delete job;
            return 0;
        }

        job->outsize = 0;
    }

    job->completion_cb = [user_completion_cb,ctx,job,out,szout,pp](uint32_t jobid, bool success) {
        if (ctx->verbose)
        {
            if (success) {
                printf("Job %u completed successfully.\n", jobid);
            } else {
                printf("Job %u failed.                \n", jobid);
            }
        }
        if (!job->output_file)
        {
            *out = pp;
            *szout = job->outsize;
        }
        if (user_completion_cb)
        {
            user_completion_cb(jobid, success);
        }
    };
    job->progress_cb = [user_progress_cb,ctx](uint32_t jobid, double progress) {
        if (ctx->verbose)
        {
            printf("Job %u progress: %.2f%%\r", jobid, progress * 100.0);
        }
        if (user_progress_cb)
        {
            user_progress_cb(jobid, progress);
        }
    };

    {
        std::lock_guard<std::mutex> lock(ctx->req_mtx);
        ctx->inflight_reqs.fetch_add(1);
    }
    ctx->req_cv.notify_all();

    ctx->queue_mtx.lock();
    jobid = job->jobid = ctx->maxjobid++;
    job->start_block = ctx->input_blocks;
    ctx->input_blocks += job->n_blocks;
    ctx->queue->push(job);
    ctx->queue_mtx.unlock();
    ctx->queue_cv.notify_all();

    return jobid;
}


extern "C" bool tsqDecompress_MT( TSQDecompressionContext_MT* ctx, uint8_t* in, size_t szin, bool infile, uint8_t** out, size_t* szout, bool outfile )
{
    if (!ctx || !in || szin == 0 || !out || !szout)
    {
        return false; // Invalid parameters
    }

    std::mutex completion_mtx;
    std::condition_variable completion_cv;
    bool finished = false;
    bool return_status;

    tsqDecompressAsync_MT( ctx, in, szin, infile, out, szout, outfile,
        [&finished,&return_status,&completion_cv](uint32_t jobid, bool success) {
            finished = true;
            return_status = success;
            completion_cv.notify_one();
        },
        nullptr
    );

    // We block until job completion
    {
        std::unique_lock<std::mutex> lock(completion_mtx);
        completion_cv.wait(lock, [&finished]{ return finished; });
    }

    return return_status; // Return the status of the compression job
}


