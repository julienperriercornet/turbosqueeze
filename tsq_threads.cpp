
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


void compression_read_worker( TSQCompressionContext_MT* ctx )
{
    while (true)
    {
        if (!ctx->exit_request)
        {
            std::unique_lock<std::mutex> lock(ctx->queue_mtx);
            ctx->queue_cv.wait(lock, [&]{ return (!ctx->queue->empty()) || ctx->exit_request; });
            if (ctx->exit_request)
                break;
        }
        else break;

        TSQJob* job = ctx->queue->front();
        FILE* input_stream = job->input_stream;
        size_t input_size = job->input_size;

        for (uint64_t i=job->start_block; i<job->start_block+job->n_blocks; i++)
        {
            uint32_t curworker = i % ctx->num_cores;

            if (!((ctx->workers[curworker].currentReadInput >= ctx->workers[curworker].currentWorkInput) && 
                (ctx->workers[curworker].currentReadInput - ctx->workers[curworker].currentWorkInput) < ctx->workers[curworker].n_inputs))
            {
                std::unique_lock<std::mutex> lock(ctx->reader_mtx);
                ctx->reader_cv.wait(lock, [curworker, ctx]{ return (ctx->workers[curworker].currentReadInput >= ctx->workers[curworker].currentWorkInput) && 
                    (ctx->workers[curworker].currentReadInput - ctx->workers[curworker].currentWorkInput) < ctx->workers[curworker].n_inputs; });
            }

            uint32_t curbuf = ctx->workers[curworker].currentReadInput % ctx->workers[curworker].n_inputs;
            uint32_t to_read = std::min( (size_t) TSQ_BLOCK_SZ, input_size - (i - job->start_block) * TSQ_BLOCK_SZ);

            ctx->workers[curworker].inputs[curbuf].job = job;

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
                        ctx->workers[curworker].inputs[curbuf].ext = job->use_extensions;
                        ctx->workers[curworker].inputs[curbuf].compression_level = job->compression_level;
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
                    ctx->workers[curworker].inputs[curbuf].buffer = job->input + (i - job->start_block) * TSQ_BLOCK_SZ;
                    ctx->workers[curworker].inputs[curbuf].size = to_read;
                    ctx->workers[curworker].inputs[curbuf].ext = job->use_extensions;
                    ctx->workers[curworker].inputs[curbuf].compression_level = job->compression_level;
                }

                // Signal the worker thread that one input buffer is ready
                ctx->workers[curworker].currentReadInput++;
                ctx->workers[curworker].input_cv.notify_one();
            }
            else
            {
                // We chose to propagate the error down the pipeline in case of an error
                ctx->workers[curworker].inputs[curbuf].buffer = nullptr;
                ctx->workers[curworker].inputs[curbuf].size = 0;
                ctx->workers[curworker].currentReadInput++;
                ctx->workers[curworker].input_cv.notify_one();
            }
        }

        ctx->queue_mtx.lock();
        ctx->queue->pop(); // Remove the completed job from the queue
        ctx->queue_mtx.unlock();
        ctx->queue_cv.notify_all(); // Notify any waiting threads that a job has been completed
    }
}


void compression_worker( uint32_t threadid, TSQCompressionContext_MT* ctx )
{
    struct TSQCompressionContext* compressctx = tsqAllocateContext();
    TSQWorker& worker = ctx->workers[threadid];

    while (true)
    {
        if (!((worker.currentReadInput > worker.currentWorkInput) || ctx->exit_request))
        {
            std::unique_lock<std::mutex> lock(worker.input_mtx);
            worker.input_cv.wait(lock, [&worker,ctx]{ return (worker.currentReadInput > worker.currentWorkInput) || ctx->exit_request; });
        }
        if (ctx->exit_request) break;

        uint32_t curin = worker.currentWorkInput % worker.n_inputs;

        if (!(((worker.currentWorkOutput - worker.currentWriteOutput) < worker.n_outputs) || ctx->exit_request))
        {
            std::unique_lock<std::mutex> lock(worker.output_mtx);
            worker.output_cv.wait(lock, [&worker,ctx]{ return ((worker.currentWorkOutput - worker.currentWriteOutput) < worker.n_outputs) || ctx->exit_request; });
        }
        if (ctx->exit_request) break;

        uint32_t curout = worker.currentWorkOutput % worker.n_outputs;

        assert(worker.inputs[curin].job != nullptr);

        uint8_t* inbuff = worker.inputs[curin].buffer;
        uint8_t *outbuff = worker.outputs[curout].filebuffer;

        worker.outputs[curout].size = 0;
        worker.outputs[curout].job = worker.inputs[curin].job;
        worker.outputs[curout].ext = worker.inputs[curin].ext;

        assert( worker.currentWorkInput == worker.currentWorkOutput );

        // Compression logic
        if (inbuff != nullptr)
        {
            tsq_init(compressctx);
            tsqEncode(compressctx, inbuff, outbuff, &worker.outputs[curout].size, worker.inputs[curin].size, worker.inputs[curin].ext);
        }

        worker.currentWorkInput++;
        ctx->reader_cv.notify_one();

        worker.currentWorkOutput++;
        worker.output_cv.notify_one();
    }

    // Cleanup after the worker thread is done
    if (compressctx) tsqDeallocateContext(compressctx);
}


void compression_write_worker( TSQCompressionContext_MT* ctx )
{
    uint32_t num_cores = ctx->num_cores;
    uint64_t i = 0;

    while (true)
    {
        uint32_t threadid = i % num_cores;
        TSQWorker& worker = ctx->workers[threadid];

        if (!((worker.currentWorkOutput > worker.currentWriteOutput) || ctx->exit_request))
        {
            std::unique_lock<std::mutex> lock(worker.output_mtx);

            // Wait until there is something to write from this worker
            worker.output_cv.wait(lock, [&worker,ctx]{
                return (worker.currentWorkOutput > worker.currentWriteOutput) || ctx->exit_request;
            });
        }
        if (ctx->exit_request) break;

        uint32_t curout = worker.currentWriteOutput % worker.n_outputs;
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
        }

        worker.currentWriteOutput++;
        worker.output_cv.notify_one();

        i++;
    }
}


extern "C" uint32_t tsqCompressAsync_MT( TSQCompressionContext_MT* ctx, uint8_t* in, size_t szin, bool infile, uint8_t** out, size_t *szout, bool outfile, bool useextensions, uint32_t level,
    std::function<void(uint32_t jobid, bool)> user_completion_cb, std::function<void(uint32_t jobid, double)> user_progress_cb )
{
    uint32_t jobid;

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

        fwrite("TSQ1", 1, 4, job->output_stream);
        fwrite(&job->n_blocks, 1, 4, job->output_stream);
        fwrite(&job->input_size, 1, sizeof(size_t), job->output_stream);
    }
    else
    {
        job->output = (uint8_t*) malloc( TSQ_OUTPUT_SZ*job->n_blocks );

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
        memcpy(job->output, "TSQ1", 4);
        memcpy(job->output + 4, &job->n_blocks, 4);
        memcpy(job->output + 8, &job->input_size, sizeof(size_t));
        job->output += 16; // Move output pointer past the header
        job->outsize += 16;
    }

    job->output_file = outfile;
    job->use_extensions = useextensions;
    job->compression_level = level;

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
        delete job;
    };
    job->progress_cb = [user_progress_cb,ctx](uint32_t jobid, double progress) {
        if (ctx->verbose)
        {
            printf("Job %u progress: %.2f%%\r", jobid, progress * 100.0);
        }
        if (user_progress_cb)
            user_progress_cb(jobid, progress);
    };

    ctx->queue_mtx.lock();
    jobid = job->jobid = ctx->maxjobid++;
    job->start_block = ctx->input_blocks;
    ctx->input_blocks += job->n_blocks;
    ctx->queue->push(job);
    ctx->queue_mtx.unlock();
    ctx->queue_cv.notify_all();

    return jobid;
}


extern "C" bool tsqCompress_MT( TSQCompressionContext_MT* ctx, uint8_t* in, size_t szin, bool infile, uint8_t** out, size_t* szout, bool outfile, bool useextensions, uint32_t level )
{
    if (!ctx || !in || szin == 0 || !out || szout == 0)
    {
        return false; // Invalid parameters
    }

    std::mutex completion_mtx;
    std::condition_variable completion_cv;
    bool finished = false;
    bool return_status;

    tsqCompressAsync_MT( ctx, in, szin, infile, out, szout, outfile, useextensions, level,
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
        if (!(!ctx->queue->empty() || ctx->exit_request))
        {
            std::unique_lock<std::mutex> lock(ctx->queue_mtx);
            ctx->queue_cv.wait(lock, [&]{ return !ctx->queue->empty() || ctx->exit_request; });
        }

        if (ctx->exit_request)
            break;

        TSQJob* job = ctx->queue->front();
        FILE* input_stream = job->input_stream;
        size_t input_size = job->input_size;

        for (uint32_t i=job->start_block; i<job->start_block+job->n_blocks; i++)
        {
            uint32_t curworker = i % ctx->num_cores;

            if (!((ctx->workers[curworker].currentReadInput >= ctx->workers[curworker].currentWorkInput) && 
                (ctx->workers[curworker].currentReadInput - ctx->workers[curworker].currentWorkInput) < ctx->workers[curworker].n_inputs))
            {
                std::unique_lock<std::mutex> lock(ctx->reader_mtx);
                ctx->reader_cv.wait(lock, [curworker, ctx]{ return (ctx->workers[curworker].currentReadInput >= ctx->workers[curworker].currentWorkInput) && 
                    (ctx->workers[curworker].currentReadInput - ctx->workers[curworker].currentWorkInput) < ctx->workers[curworker].n_inputs; });
            }

            uint32_t to_read;
            uint32_t with_extensions;
            uint32_t curbuf = ctx->workers[curworker].currentReadInput % ctx->workers[curworker].n_inputs;
            ctx->workers[curworker].inputs[curbuf].job = job;

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

            ctx->workers[curworker].currentReadInput++;
            ctx->workers[curworker].input_cv.notify_one();
        }

        ctx->queue_mtx.lock();
        ctx->queue->pop(); // Remove the completed job from the queue
        ctx->queue_mtx.unlock();
        ctx->queue_cv.notify_all(); // Notify any waiting threads that a job has been completed
    }
}


void decompression_worker( uint32_t threadid, TSQDecompressionContext_MT* ctx )
{
    while (true)
    {
        if (!(ctx->workers[threadid].currentReadInput > ctx->workers[threadid].currentWorkInput || ctx->exit_request))
        {
            std::unique_lock<std::mutex> lock(ctx->workers[threadid].input_mtx);
            ctx->workers[threadid].input_cv.wait(lock, [ctx,threadid]{ return ctx->workers[threadid].currentReadInput > ctx->workers[threadid].currentWorkInput || ctx->exit_request; });
        }

        if (ctx->exit_request) break;

        uint32_t curbuf = ctx->workers[threadid].currentWorkInput % ctx->workers[threadid].n_inputs;

        if (!((ctx->workers[threadid].currentWorkOutput - ctx->workers[threadid].currentWriteOutput) < ctx->workers[threadid].n_outputs || ctx->exit_request))
        {
            std::unique_lock<std::mutex> lock(ctx->workers[threadid].output_mtx);
            ctx->workers[threadid].output_cv.wait(lock, [ctx,threadid]{ return (ctx->workers[threadid].currentWorkOutput - ctx->workers[threadid].currentWriteOutput) < ctx->workers[threadid].n_outputs || ctx->exit_request; });
        }

        if (ctx->exit_request) break;

        uint32_t curout = ctx->workers[threadid].currentWorkOutput % ctx->workers[threadid].n_outputs;

        uint8_t* inbuff = ctx->workers[threadid].inputs[curbuf].buffer;
        uint8_t* outbuff = ctx->workers[threadid].outputs[curout].filebuffer; // TODO: speed optimization possible here

        ctx->workers[threadid].outputs[curout].job = ctx->workers[threadid].inputs[curbuf].job;
        ctx->workers[threadid].outputs[curout].size = 0;

        assert( ctx->workers[threadid].currentWorkInput == ctx->workers[threadid].currentWorkOutput );

        // Decompression logic
        if (inbuff != nullptr)
        {
            tsqDecode( inbuff, outbuff, &ctx->workers[threadid].outputs[curout].size, ctx->workers[threadid].inputs[curbuf].size, ctx->workers[threadid].inputs[curbuf].ext );
        }

        ctx->workers[threadid].currentWorkInput++;
        ctx->reader_cv.notify_one();

        ctx->workers[threadid].currentWorkOutput++;
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

        if (!(worker.currentWorkOutput > worker.currentWriteOutput || ctx->exit_request))
        {
            std::unique_lock<std::mutex> lock(worker.output_mtx);

            // Wait until there is something to write from this worker
            worker.output_cv.wait(lock, [&worker,ctx]{
                return worker.currentWorkOutput > worker.currentWriteOutput || ctx->exit_request;
            });
        }

        if (ctx->exit_request) break;

        uint32_t curout = worker.currentWriteOutput % worker.n_outputs;
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
                // TODO: speed optimization possible here
                memcpy(job->output, outbuff, outsize);
                job->output += outsize; // Move output pointer past the written data 
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
        }

        worker.currentWriteOutput++;
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
    char magic_key[5];
    magic_key[0] = 'T';
    magic_key[1] = 'S';
    magic_key[2] = 'Q';
    magic_key[3] = '1';
    magic_key[4] = 0;

    uint32_t n_blocks = 0;

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
        uint32_t read_is = fread(&job->outsize, 1, sizeof(size_t), job->input_stream);

        if (strncmp(&magic[0], &magic_key[0], 4) != 0)
        {
            if (ctx->verbose)
            {
                printf("Error: signature mismatch (%s but expected %s).\n", &magic[0], &magic_key[0]);
            }
            if (user_completion_cb)
                user_completion_cb(0, false);
            delete job;
            return 0;
        }
    }
    else
    {
        if (memcmp(job->input, &magic_key[0], 4) != 0)
        {
            if (user_completion_cb)
                user_completion_cb(0, false);
            delete job;
            return 0;
        }

        memcpy(&n_blocks, job->input + 4, 4);
        memcpy(&job->outsize, job->input + 8, sizeof(size_t));
        job->input += 16; // Move input pointer past the header
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
        job->output = (uint8_t*) malloc( job->outsize+32 );
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
        delete job;
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


