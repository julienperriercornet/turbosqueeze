
/*
** Turbosqueeze thread functions.
** Copyright (C) 2024-2025 Julien Perrier-cornet
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
    uint32_t last_job_id = 0xFFFFFFFF;

    while (true)
    {
        if (!ctx->exit_request)
        {
            std::unique_lock<std::mutex> lock(ctx->queue_mtx);
            ctx->queue_cv.wait(lock, [&]{ return (ctx->currentjob && ctx->currentjob->jobid != last_job_id) || ctx->exit_request; });
            if (ctx->exit_request)
                break;
        }
        else break;

        assert( ctx->currentjob );
        last_job_id = ctx->currentjob->jobid;

        TSQJob& job = *ctx->currentjob;
        FILE* inpput_stream = job.input_stream;
        size_t input_size = job.input_size;

        for (uint32_t i=0; i<ctx->input_blocks; i++)
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
            uint8_t* inbuff = ctx->workers[curworker].inputs[curbuf].filebuffer;

            uint32_t to_read = std::min( (size_t) TSQ_BLOCK_SZ, input_size - i*TSQ_BLOCK_SZ);

            if (to_read > 0 && to_read<=TSQ_BLOCK_SZ)
            {
                if (job.input_stream)
                {
                    to_read = fread(inbuff, 1, to_read, job.input_stream);
                    ctx->workers[curworker].inputs[curbuf].buffer = inbuff;
                    ctx->workers[curworker].inputs[curbuf].size = to_read;
                    ctx->workers[curworker].inputs[curbuf].ext = job.use_extensions;
                    ctx->workers[curworker].inputs[curbuf].compression_level = job.compression_level;
                }
                else
                {
                    ctx->workers[curworker].inputs[curbuf].buffer = job.input + i * TSQ_BLOCK_SZ;
                    ctx->workers[curworker].inputs[curbuf].size = to_read;
                    ctx->workers[curworker].inputs[curbuf].ext = job.use_extensions;
                    ctx->workers[curworker].inputs[curbuf].compression_level = job.compression_level;
                }

                ctx->workers[curworker].currentReadInput++;
                // Signal the worker thread that one input buffer is ready
                ctx->workers[curworker].input_cv.notify_one();
            }
            else
                break; // No more data to read
        }
    }
}


void compression_worker( uint32_t threadid, TSQCompressionContext_MT* ctx )
{
    struct TSQCompressionContext* compressctx = tsqAllocateContext();

    uint32_t last_job_id = 0xFFFFFFFF;

    while (true)
    {
        if (!ctx->exit_request)
        {
            std::unique_lock<std::mutex> lock(ctx->queue_mtx);
            ctx->queue_cv.wait(lock, [&]{ return (ctx->currentjob && ctx->currentjob->jobid != last_job_id) || ctx->exit_request; });
            if (ctx->exit_request)
                break;
        }
        else break;

        TSQJob& job = *ctx->currentjob;
        last_job_id = ctx->currentjob->jobid;

        for (uint32_t i = 0; i < ctx->workers[threadid].blocksPerWorker; ++i)
        {
            TSQWorker& worker = ctx->workers[threadid];

            if (!(worker.currentReadInput > worker.currentWorkInput))
            {
                std::unique_lock<std::mutex> lock(worker.input_mtx);
                worker.input_cv.wait(lock, [&worker]{ return worker.currentReadInput > worker.currentWorkInput; });
            }

            uint32_t curbuf = worker.currentWorkInput % worker.n_inputs;

            if (!((worker.currentWorkOutput - worker.currentWriteOutput) < worker.n_outputs))
            {
                std::unique_lock<std::mutex> lock(worker.output_mtx);
                worker.output_cv.wait(lock, [&worker]{ return (worker.currentWorkOutput - worker.currentWriteOutput) < worker.n_outputs; });
            }

            uint32_t curout = worker.currentWorkOutput % worker.n_outputs;

            uint8_t* inbuff = job.input_file ? worker.inputs[curbuf].filebuffer : worker.inputs[curbuf].buffer;
            uint8_t *outbuff = worker.outputs[curout].filebuffer;

            assert( worker.currentWorkInput == worker.currentWorkOutput );

            // Compression logic
            tsq_init(compressctx);
            tsqEncode(compressctx, inbuff, outbuff, &worker.outputs[curout].size, worker.inputs[curbuf].size, worker.inputs[curbuf].ext);

            worker.outputs[curout].ext = worker.inputs[curbuf].ext;

            worker.currentWorkInput++;
            ctx->reader_cv.notify_one();

            worker.currentWorkOutput++;
            worker.output_cv.notify_one();
        }
    }

    // Cleanup after the worker thread is done
    if (compressctx) tsqDeallocateContext(compressctx);
}


void compression_write_worker( TSQCompressionContext_MT* ctx )
{
    uint32_t num_cores = ctx->num_cores;

    while (true)
    {
        {
            std::unique_lock<std::mutex> lock(ctx->queue_mtx);
            ctx->queue_cv.wait(lock, [&]{ return !ctx->queue->empty() || ctx->exit_request; });
            if (ctx->exit_request)
                break;
        }

        TSQJob& job = *ctx->queue->front();

        FILE* out = nullptr;
        size_t inputSize = 0;
        FILE* input_stream = nullptr;
        size_t input_size = 0;
        uint32_t n_blocks;
        uint8_t *pp = nullptr;

        if (job.input_file)
        {
            input_stream = fopen((const char*) job.input, "rb");
            if (!input_stream)
            {
                if (ctx->verbose)
                {
                    printf("Error: couldn\'t open input file: %s\n", job.input);
                }
                job.completion_cb(job.jobid, false);
                goto flushcurjob; // Skip if input file cannot be opened
            }

            fseek(input_stream, 0, SEEK_END);
            input_size = ftell(input_stream);
            fseek(input_stream, 0, SEEK_SET);
            if (input_size == 0)
            {
                fclose(input_stream);
                job.completion_cb(job.jobid, false);
                goto flushcurjob; // Skip empty files
            }
        }
        else
        {
            // If input is a memory block, we assume it is already allocated and filled
            if (job.input == nullptr || job.size == 0)
            {
                job.completion_cb(job.jobid, false);
                goto flushcurjob;
            }
            input_size = job.size;
        }

        job.input_size = input_size; // Store input size in job for later use
        job.input_stream = input_stream;
        inputSize = job.input_size;

        n_blocks = (input_size % TSQ_BLOCK_SZ != 0 ? 1 : 0) + (input_size / TSQ_BLOCK_SZ);

        // Initialize workers
        for (uint32_t i = 0; i < ctx->num_cores; ++i) {
            ctx->workers[i].currentReadInput = 0;
            ctx->workers[i].currentWorkInput = 0;
            ctx->workers[i].currentWorkOutput = 0;
            ctx->workers[i].currentWriteOutput = 0;
            ctx->workers[i].blocksPerWorker = (n_blocks / ctx->num_cores) + (i < n_blocks % ctx->num_cores ? 1 : 0);
        }

        ctx->input_blocks = n_blocks;
        ctx->blocks_writen = 0;

        if (job.output_file)
        {
            out = fopen((const char*) job.output, "wb");
            if (!out)
            {
                if (ctx->verbose)
                {
                    printf("Error: couldn\'t open output file: %s\n", job.output);
                }
                job.completion_cb(job.jobid, false);
                goto flushcurjob; // No blocks to process
            }
        }
        else
        {
            job.output = (uint8_t*) malloc( n_blocks*TSQ_OUTPUT_SZ );
            job.outsize = 0;
            if (job.output == nullptr)
            {
                job.completion_cb(job.jobid, false);
                goto flushcurjob; // No blocks to process
            }
            pp = job.output;
        }

        if (n_blocks == 0)
        {
            job.completion_cb(job.jobid, false);
            goto flushcurjob; // No blocks to process
        }

        ctx->currentjob = ctx->queue->front();
        ctx->queue_cv.notify_all(); // Notify all threads that a job is available

        // --- Write TSQ1 header ---
        if (out) {
            fwrite("TSQ1", 1, 4, out);
            fwrite(&n_blocks, 1, 4, out);
            fwrite(&inputSize, 1, sizeof(size_t), out);
        } else {
            memcpy(job.output, "TSQ1", 4);
            memcpy(job.output + 4, &n_blocks, 4);
            memcpy(job.output + 8, &inputSize, sizeof(size_t));
            job.output += 16; // Move output pointer past the header
            job.outsize += 16;
        }

        for (uint32_t i = 0; i < n_blocks; ++i)
        {
            uint32_t threadid = i % num_cores;
            TSQWorker& worker = ctx->workers[threadid];

            if (!(worker.currentWorkOutput > worker.currentWriteOutput))
            {
                std::unique_lock<std::mutex> lock(worker.output_mtx);

                // Wait until there is something to write from this worker
                worker.output_cv.wait(lock, [&worker]{
                    return worker.currentWorkOutput > worker.currentWriteOutput;
                });
            }

            uint32_t curout = worker.currentWriteOutput % worker.n_outputs;
            uint8_t* outbuff = worker.outputs[curout].filebuffer;
            uint32_t outsize = worker.outputs[curout].size;
            uint32_t outmask = outsize;
            if (worker.outputs[curout].ext) outmask |= 0x800000;

            // Write the size header (3 bytes, as in compress())
            if (out)
            {
                fputc(outmask & 0xFF, out);
                fputc((outmask >> 8) & 0xFF, out);
                fputc((outmask >> 16) & 0xFF, out);
                fwrite(outbuff, 1, outsize, out);
                job.outsize += 3 + outsize;
            }
            else
            {
                job.output[0] = outmask & 0xFF;
                job.output[1] = (outmask >> 8) & 0xFF;
                job.output[2] = (outmask >> 16) & 0xFF;
                memcpy(job.output + 3, outbuff, outsize);
                job.output += outsize + 3; // Move output pointer past the written data 
                job.outsize += 3 + outsize;
            }

            worker.currentWriteOutput++;
            worker.output_cv.notify_one();

            ctx->blocks_writen++;
            job.progress_cb(job.jobid, (double) (ctx->blocks_writen+1) / ctx->input_blocks);
        }

        if (job.output_file)
        {
            /*
            fseek( out, 8, SEEK_SET );
            fwrite(&job.outsize, 1, sizeof(size_t), out);
            fclose(out);
            */
            out = nullptr;
        }
        else
        {
            //memcpy(pp + 8, &job.outsize, sizeof(size_t));
            job.output = pp;
        }

        ctx->currentjob = nullptr; // Clear current job after completion
        job.completion_cb(job.jobid, true); // Notify completion

flushcurjob:
        ctx->currentjob = nullptr; // Clear current job after completion
        ctx->queue_mtx.lock();
        ctx->queue->pop(); // Remove the completed job from the queue
        ctx->queue_mtx.unlock();
        ctx->queue_cv.notify_all(); // Notify any waiting threads that a job has been completed
    }
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

    struct TSQJob job;
    job.input = in;
    job.size = szin;
    job.input_file = infile;
    if (outfile)
    {
        job.output = *out;
        job.outsize = *szout;
    }
    else
    {
        job.output = nullptr;
        job.outsize = 0;
    }
    job.output_file = outfile;
    job.use_extensions = useextensions;
    job.compression_level = level;
    job.jobid = ctx->maxjobid++;
    job.completion_cb = [&finished,&return_status,&completion_cv,&ctx](uint32_t jobid, bool success) {
        finished = true;
        return_status = success;
        completion_cv.notify_one();
        if (ctx->verbose)
        {
            if (success) {
                printf("Job %u completed successfully.\n", jobid);
            } else {
                printf("Job %u failed.                \n", jobid);
            }
        }
    };
    job.progress_cb = [&ctx](uint32_t jobid, double progress) {
        if (ctx->verbose)
        {
            printf("Job %u progress: %.2f%%\r", jobid, progress * 100.0);
        }
    };

    ctx->queue_mtx.lock();
    ctx->queue->push(&job);
    ctx->queue_mtx.unlock();
    ctx->queue_cv.notify_all();

    // We block until job completion
    {
        std::unique_lock<std::mutex> lock(completion_mtx);
        completion_cv.wait(lock, [&finished]{ return finished; });
    }

    if (!outfile)
    {
        *out = job.output;
        *szout = job.outsize;
    }

    return return_status; // Return the status of the compression job
}


uint32_t tsqCompressAsync_MT( TSQCompressionContext_MT* ctx, uint8_t* in, size_t szin, bool infile, uint8_t** out, size_t *szout, bool outfile, bool useextensions, uint32_t level,
    std::function<void(uint32_t jobid, bool)> user_completion_cb, std::function<void(uint32_t jobid, double)> user_progress_cb )
{
    uint32_t jobid;

    struct TSQJob *job = (struct TSQJob*) malloc( sizeof(struct TSQJob) );

    if (!job) return 0;

    job->input = in;
    job->size = szin;
    job->input_file = infile;
    if (outfile)
    {
        job->output = *out;
        job->outsize = *szout;
    }
    else
    {
        job->output = nullptr;
        job->outsize = 0;
    }
    job->output_file = outfile;
    job->use_extensions = useextensions;
    job->compression_level = level;
    jobid = job->jobid = ctx->maxjobid++;
    job->completion_cb = [user_completion_cb,ctx,job,out,szout](uint32_t jobid, bool success) {
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
            *out = job->output;
            *szout = job->outsize;
        }
        if (user_completion_cb)
        {
            user_completion_cb(jobid, success);
        }
        free( job );
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
    ctx->queue->push(job);
    ctx->queue_mtx.unlock();
    ctx->queue_cv.notify_all();

    return jobid;
}


void decompression_read_worker( TSQDecompressionContext_MT* ctx )
{
    uint32_t last_job_id = 0xFFFFFFFF;

    while (true)
    {
        if (!ctx->exit_request)
        {
            std::unique_lock<std::mutex> lock(ctx->queue_mtx);
            ctx->queue_cv.wait(lock, [&]{ return (ctx->currentjob && ctx->currentjob->jobid != last_job_id) || ctx->exit_request; });
            if (ctx->exit_request)
                break;
        }
        else break;

        assert( ctx->currentjob );
        last_job_id = ctx->currentjob->jobid;

        TSQJob& job = *ctx->currentjob;
        FILE* inpput_stream = job.input_stream;
        size_t input_size = job.input_size;

        for (uint32_t i=0; i<ctx->input_blocks; i++)
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
            uint8_t* inbuff = ctx->workers[curworker].inputs[curbuf].filebuffer;

            uint32_t to_read;
            uint32_t with_extensions;

            if (job.input_stream)
            {
                to_read = fgetc( job.input_stream );
                to_read |= fgetc( job.input_stream ) << 8;
                to_read |= fgetc( job.input_stream ) << 16;
                with_extensions = (to_read & 0x800000) != 0;
                to_read &= 0x7FFFFF;

                if (to_read>0 && to_read<=TSQ_OUTPUT_SZ)
                {
                    uint32_t actually_read = fread(inbuff, 1, to_read, job.input_stream);
                    if (actually_read != to_read) break;
                    ctx->workers[curworker].inputs[curbuf].buffer = inbuff;
                    ctx->workers[curworker].inputs[curbuf].size = to_read;
                    ctx->workers[curworker].inputs[curbuf].ext = with_extensions;
                }
                else
                {
                    if (ctx->verbose)
                    {
                        printf("Read error. (to_read %u)\n", to_read);
                    }
                    break;
                }
            }
            else
            {
                to_read = job.input[0];
                to_read |= job.input[1] << 8;
                to_read |= job.input[2] << 16;
                with_extensions = (to_read & 0x800000) != 0;
                to_read &= 0x7FFFFF;

                if (to_read > 0 && to_read<=TSQ_OUTPUT_SZ)
                {
                    ctx->workers[curworker].inputs[curbuf].buffer = job.input + 3;
                    ctx->workers[curworker].inputs[curbuf].size = to_read;
                    ctx->workers[curworker].inputs[curbuf].ext = with_extensions;
                    job.input += to_read + 3;
                }
                else
                    break;
            }

            ctx->workers[curworker].currentReadInput++;
            // Signal the worker thread that one input buffer is ready
            ctx->workers[curworker].input_cv.notify_one();
        }
    }

}

void decompression_worker( uint32_t threadid, TSQDecompressionContext_MT* ctx )
{
    uint32_t last_job_id = 0xFFFFFFFF;

    while (true)
    {
        if (!ctx->exit_request)
        {
            std::unique_lock<std::mutex> lock(ctx->queue_mtx);
            ctx->queue_cv.wait(lock, [&]{ return (ctx->currentjob && ctx->currentjob->jobid != last_job_id) || ctx->exit_request; });
            if (ctx->exit_request)
                break;
        }
        else break;

        TSQJob& job = *ctx->currentjob;
        last_job_id = ctx->currentjob->jobid;

        for (uint32_t i = 0; i < ctx->workers[threadid].blocksPerWorker; ++i)
        {
            if (!(ctx->workers[threadid].currentReadInput > ctx->workers[threadid].currentWorkInput))
            {
                ctx->reader_cv.notify_all();
                std::unique_lock<std::mutex> lock(ctx->workers[threadid].input_mtx);
                ctx->workers[threadid].input_cv.wait(lock, [&]{ return ctx->workers[threadid].currentReadInput > ctx->workers[threadid].currentWorkInput; });
            }

            uint32_t curbuf = ctx->workers[threadid].currentWorkInput % ctx->workers[threadid].n_inputs;

            if (!((ctx->workers[threadid].currentWorkOutput - ctx->workers[threadid].currentWriteOutput) < ctx->workers[threadid].n_outputs))
            {
                std::unique_lock<std::mutex> lock(ctx->workers[threadid].output_mtx);
                ctx->workers[threadid].output_cv.wait(lock, [&]{ return (ctx->workers[threadid].currentWorkOutput - ctx->workers[threadid].currentWriteOutput) < ctx->workers[threadid].n_outputs; });
            }

            uint32_t curout = ctx->workers[threadid].currentWorkOutput % ctx->workers[threadid].n_outputs;

            uint8_t* inbuff = ctx->workers[threadid].inputs[curbuf].buffer;
            uint8_t *outbuff = ctx->workers[threadid].outputs[curout].filebuffer; // TODO: speed optimization possible here

            assert( ctx->workers[threadid].currentWorkInput == ctx->workers[threadid].currentWorkOutput );

            // Decompression logic
            tsqDecode( inbuff, outbuff, &ctx->workers[threadid].outputs[curout].size, ctx->workers[threadid].inputs[curbuf].size, ctx->workers[threadid].inputs[curbuf].ext );

            ctx->workers[threadid].currentWorkInput++;
            ctx->workers[threadid].input_cv.notify_all();
            ctx->reader_cv.notify_all();

            ctx->workers[threadid].currentWorkOutput++;
            ctx->workers[threadid].output_cv.notify_one();
        }
    }
}


void decompression_write_worker( TSQDecompressionContext_MT* ctx )
{
    uint32_t num_cores = ctx->num_cores;

    while (true)
    {
        if (!ctx->exit_request)
        {
            std::unique_lock<std::mutex> lock(ctx->queue_mtx);
            ctx->queue_cv.wait(lock, [&]{ return !ctx->queue->empty() || ctx->exit_request; });
            if (ctx->exit_request)
                break;
        }
        else break;

        TSQJob& job = *ctx->queue->front();

        FILE* out = nullptr;
        size_t inputSize = 0;
        FILE* input_stream = nullptr;
        size_t input_size = 0;
        uint32_t n_blocks;
        uint8_t *pp;

        if (job.input_file)
        {
            input_stream = fopen((const char*) job.input, "rb");
            if (!input_stream)
            {
                if (ctx->verbose)
                {
                    printf("Error opening input file: %s\n", job.input);
                }
                job.completion_cb(job.jobid, false);
                goto flushcurjob; // Skip if input file cannot be opened
            }

            fseek(input_stream, 0, SEEK_END);
            input_size = ftell(input_stream);
            fseek(input_stream, 0, SEEK_SET);
            if (input_size == 0)
            {
                fclose(input_stream);
                if (ctx->verbose)
                {
                    printf("Error: input file empty.\n");
                }
                job.completion_cb(job.jobid, false);
                goto flushcurjob; // Skip empty files
            }
        }
        else
        {
            // If input is a memory block, we assume it is already allocated and filled
            if (job.input == nullptr || job.size < 16)
            {
                job.completion_cb(job.jobid, false);
                goto flushcurjob;
            }
            input_size = job.size;
        }

        job.input_size = input_size; // Store input size in job for later use
        job.input_stream = input_stream;

        if (job.output_file)
        {
            out = fopen((const char*) job.output, "wb");
            if (!out)
            {
                if (ctx->verbose)
                {
                    printf("Error opening output file: %s\n", job.output);
                }
                job.completion_cb(job.jobid, false);
                goto flushcurjob; // No blocks to process
            }
        }

        char magic[5];
        magic[4] = 0;
        char magic_key[5];
        magic_key[0] = 'T';
        magic_key[1] = 'S';
        magic_key[2] = 'Q';
        magic_key[3] = '1';
        magic_key[4] = 0;

        if (input_stream) {
            uint32_t read_magic = fread(&magic[0], 1, 4, input_stream);
            uint32_t read_block = fread(&n_blocks, 1, 4, input_stream);
            uint32_t read_is = fread(&inputSize, 1, sizeof(size_t), input_stream);
            if (strncmp(&magic[0], &magic_key[0], 4) != 0)
            {
                if (ctx->verbose)
                {
                    printf("Error: signature mismatch (%s but expected %s).\n", &magic[0], &magic_key[0]);
                }
                job.completion_cb(job.jobid, false);
                goto flushcurjob;
            }
        } else {
            if (memcmp(job.input, &magic_key[0], 4) != 0)
            {
                job.completion_cb(job.jobid, false);
                goto flushcurjob;
            }
            memcpy(&n_blocks, job.input + 4, 4);
            memcpy(&inputSize, job.input + 8, sizeof(size_t));
            job.input += 16; // Move input pointer past the header
            job.output = (uint8_t*) malloc( inputSize );
            job.outsize = 0;
            pp = job.output;
        }

        if (job.output == nullptr)
        {
            job.completion_cb(job.jobid, false);
            goto flushcurjob; // No blocks to process
        }

        if (n_blocks == 0)
        {
            if (ctx->verbose)
            {
                printf("Error: no blocks to decode in input file.\n");
            }
            job.completion_cb(job.jobid, false);
            goto flushcurjob; // No blocks to process
        }

        // Initialize workers
        for (uint32_t i = 0; i < ctx->num_cores; ++i)
        {
            ctx->workers[i].currentReadInput = 0;
            ctx->workers[i].currentWorkInput = 0;
            ctx->workers[i].currentWorkOutput = 0;
            ctx->workers[i].currentWriteOutput = 0;
            ctx->workers[i].blocksPerWorker = (n_blocks / ctx->num_cores) + (i < n_blocks % ctx->num_cores ? 1 : 0);
        }

        ctx->input_blocks = n_blocks;
        ctx->blocks_writen = 0;

        ctx->currentjob = ctx->queue->front();
        ctx->queue_cv.notify_all(); // Notify all threads that a job is available

        for (uint32_t i = 0; i < n_blocks; ++i)
        {
            uint32_t threadid = i % num_cores;
            TSQWorker& worker = ctx->workers[threadid];

            if (!(worker.currentWorkOutput > worker.currentWriteOutput))
            {
                std::unique_lock<std::mutex> lock(worker.output_mtx);

                // Wait until there is something to write from this worker
                worker.output_cv.wait(lock, [&worker]{
                    return worker.currentWorkOutput > worker.currentWriteOutput;
                });
            }

            uint32_t curout = worker.currentWriteOutput % worker.n_outputs;
            uint8_t* outbuff = worker.outputs[curout].filebuffer;
            uint32_t outsize = worker.outputs[curout].size;

            // Write the size header (3 bytes, as in compress())
            if (out)
            {
                fwrite(outbuff, 1, outsize, out);
            }
            else
            {
                // TODO: speed optimization possible here
                memcpy(job.output, outbuff, outsize);
                job.output += outsize; // Move output pointer past the written data 
                job.outsize += outsize;
            }

            worker.currentWriteOutput++;
            worker.output_cv.notify_one();

            ctx->blocks_writen++;
            job.progress_cb(job.jobid, (double) (ctx->blocks_writen+1) / ctx->input_blocks);
        }

        if (!out)
            job.output = pp;

        ctx->currentjob = nullptr; // Clear current job after completion from the queue
        job.completion_cb(job.jobid, true); // Notify completion
        if (out)
        {
            fclose(out);
            out = nullptr;
        }

flushcurjob:
        ctx->currentjob = nullptr; // Clear current job after completion
        ctx->queue_mtx.lock();
        ctx->queue->pop(); // Remove the completed job from the queue
        ctx->queue_mtx.unlock();
        ctx->queue_cv.notify_all(); // Notify any waiting threads that a job has been completed, and maybe there's another one to start processing
    }
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

    struct TSQJob job;
    job.input = in;
    job.size = szin;
    job.input_file = infile;
    if (outfile)
    {
        job.output = *out;
        job.outsize = *szout;
    }
    else
    {
        job.output = nullptr;
        job.outsize = 0;
    }
    job.output_file = outfile;
    job.jobid = ctx->maxjobid++;
    job.completion_cb = [&finished,&return_status,&completion_cv,&ctx](uint32_t jobid, bool success) {
        finished = true;
        return_status = success;
        completion_cv.notify_one();
        if (ctx->verbose)
        {
            if (success) {
                printf("Job %u completed successfully.\n", jobid);
            } else {
                printf("Job %u failed.                \n", jobid);
            }
        }
    };
    job.progress_cb = [&ctx](uint32_t jobid, double progress) {
        if (ctx->verbose)
        {
            printf("Job %u progress: %.2f%%\r", jobid, progress * 100.0);
        }
    };

    ctx->queue_mtx.lock();
    ctx->queue->push(&job);
    ctx->queue_mtx.unlock();
    ctx->queue_cv.notify_all();

    // We wait until job completion
    {
        std::unique_lock<std::mutex> lock(completion_mtx);
        completion_cv.wait(lock, [&finished]{ return finished; });
    }

    if (!outfile)
    {
        *szout = job.outsize;
        *out = job.output;
    }

    return return_status; // Return the status of the compression job
}


uint32_t tsqDecompressAsync_MT( TSQDecompressionContext_MT* ctx, uint8_t* in, size_t szin, bool infile, uint8_t** out, size_t* szout, bool outfile,
    std::function<void(uint32_t jobid, bool)> user_completion_cb, std::function<void(uint32_t jobid, double)> user_progress_cb )
{
    uint32_t jobid;

    struct TSQJob *job = (struct TSQJob*) malloc( sizeof(struct TSQJob) );

    if (!job) return 0;

    job->input = in;
    job->size = szin;
    job->input_file = infile;
    if (outfile)
    {
        job->output = *out;
        job->outsize = *szout;
    }
    else
    {
        job->output = nullptr;
        job->outsize = 0;
    }
    job->output_file = outfile;
    job->use_extensions = false;
    job->compression_level = 0;
    jobid = job->jobid = ctx->maxjobid++;
    job->completion_cb = [user_completion_cb,ctx,job,out,szout](uint32_t jobid, bool success) {
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
            *out = job->output;
            *szout = job->outsize;
        }
        if (user_completion_cb)
        {
            user_completion_cb(jobid, success);
        }
        free( job );
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
    ctx->queue->push(job);
    ctx->queue_mtx.unlock();
    ctx->queue_cv.notify_all();

    return jobid;
}
