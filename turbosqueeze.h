#pragma once

/*
 * Turbosqueeze API.
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


/*
 * \struct TSQCompressionContext
 * @brief Low-level compression context for single-threaded operations.
 *
 * This structure holds the state required for block-based compression.
 * It is typically allocated and managed by the API functions.
 *
 * Fields:
 *   refhash - Pointer to a hash table used for reference matching during compression.
 *             The table size is defined by TSQ_HASH_SZ and is used to accelerate pattern matching.
 */
struct TSQCompressionContext {
    /**
     * Pointer to the reference hash table used for fast pattern matching during compression.
     * The table is typically allocated internally and should not be modified directly by the user.
     */
    uint16_t *refhash;
};

class TSQJob;

/*
 * \struct TSQBuffer
 * @brief Buffer structure for holding data blocks during compression or decompression.
 *
 * Used internally by worker threads to manage input and output data.
 *
 * Fields:
 *   buffer - Pointer to the main data buffer (input or output block).
 *   filebuffer - Pointer to a file buffer, if file I/O is involved.
 *   size - Size of the data in the buffer, in bytes.
 *   ext - Extension flags or metadata for the buffer (e.g., format extensions).
 *   compression_level - Compression level to be used for this buffer, if applicable.
 */
struct TSQBuffer {
    /**
     * Pointer to the main data buffer (input or output block).
     */
    uint8_t* buffer;
    /**
     * Pointer to a file buffer, used when reading from or writing to files.
     */
    uint8_t* filebuffer;
    /**
     * Pointer to the job.
     */
    TSQJob* job;
    /**
     * Size of the data in the buffer, in bytes.
     */
    uint32_t size;
    /**
     * Extension flags or metadata for the buffer (e.g., format extensions).
     */
    uint32_t ext;
    /**
     * Compression level to be used for this buffer, if applicable.
     */
    uint32_t compression_level;
};

/*
 * \struct TSQWorker
 * @brief Worker structure for multi-threaded compression/decompression.
 *
 * Implements a producer-consumer pattern with separate input and output buffer queues.
 *
 * Fields:
 *   inputs - Vector of input buffers to be processed by the worker.
 *   n_inputs - Number of input buffers assigned to this worker.
 *   currentReadInput - Index of the next input buffer to be read.
 *   currentWorkInput - Index of the input buffer currently being processed.
 *   input_mtx - Mutex for synchronizing access to input buffers.
 *   input_cv - Condition variable for input buffer availability.
 *   outputs - Vector of output buffers produced by the worker.
 *   n_outputs - Number of output buffers assigned to this worker.
 *   currentWorkOutput - Index of the output buffer currently being written.
 *   currentWriteOutput - Index of the next output buffer to be written to disk or memory.
 *   output_mtx - Mutex for synchronizing access to output buffers.
 *   output_cv - Condition variable for output buffer availability.
 *   blocksPerWorker - Number of data blocks assigned to this worker for processing.
 */
struct TSQWorker {
    /**
     * Vector of input buffers to be processed by the worker.
     */
    std::vector<struct TSQBuffer> inputs;
    /**
     * Number of input buffers assigned to this worker.
     */
    uint32_t n_inputs;
    /**
     * Index of the next input buffer to be read by the worker.
     * Volatile for safe concurrent access.
     */
    volatile uint64_t currentReadInput;
    /**
     * Index of the input buffer currently being processed.
     * Volatile for safe concurrent access.
     */
    volatile uint64_t currentWorkInput;
    /**
     * Mutex for synchronizing access to input buffers.
     */
    std::mutex input_mtx;
    /**
     * Condition variable for input buffer availability.
     */
    std::condition_variable input_cv;

    /**
     * Vector of output buffers produced by the worker.
     */
    std::vector<struct TSQBuffer> outputs;
    /**
     * Number of output buffers assigned to this worker.
     */
    uint32_t n_outputs;
    /**
     * Index of the output buffer currently being written.
     * Volatile for safe concurrent access.
     */
    volatile uint64_t currentWorkOutput;
    /**
     * Index of the next output buffer to be written to disk or memory.
     * Volatile for safe concurrent access.
     */
    volatile uint64_t currentWriteOutput;
    /**
     * Mutex for synchronizing access to output buffers.
     */
    std::mutex output_mtx;
    /**
     * Condition variable for output buffer availability.
     */
    std::condition_variable output_cv;

    /**
     * Number of data blocks assigned to this worker for processing.
     */
    uint32_t blocksPerWorker;
};

/*
 * \struct TSQJob
 * @brief Job descriptor for asynchronous or multi-threaded compression/decompression.
 *
 * Represents a single unit of work, which may be a file or a memory buffer.
 *
 * Fields:
 *   input - Pointer to input data buffer or filename (if input_file is true).
 *   size - Size of the input data in bytes.
 *   input_file - If true, 'input' is a filename; if false, a memory buffer.
 *   jobid - Unique job ID for tracking asynchronous processing.
 *   use_extensions - If true, enables format extensions for this job.
 *   compression_level - Compression level to use for this job.
 *   input_stream - File stream for input, if applicable.
 *   input_size - Size of the input data (redundant with 'size' in some cases).
 *   output - Pointer to output data buffer or filename (if output_file is true).
 *   outsize - Size of the output data in bytes.
 *   output_file - If true, 'output' is a filename; if false, a memory buffer.
 *   completion_cb - Callback function invoked upon job completion (jobid, success).
 *   progress_cb - Callback function invoked to report progress (jobid, progress [0.0-1.0]).
 */
class TSQJob {
public:

    /**
     * Constructor initializes members to default values.
     */
    TSQJob() : input(nullptr), size(0), input_file(false), jobid(0), use_extensions(false), compression_level(0), input_stream(nullptr),
        input_size(0), start_block(0), n_blocks(0), output(nullptr), outsize(0), output_file(false), output_stream(nullptr), error_occurred(false),
        completion_cb(nullptr), progress_cb(nullptr)
    {
    }

    /**
     * Destructor cleans up file streams if necessary.
     */
    ~TSQJob()
    {
        if (input_file && input_stream)
        {
            fclose(input_stream);
            input_stream = nullptr;
        }
        if (output_file && output_stream)
        {
            fclose(output_stream);
            output_stream = nullptr;
        }
    }

    /**
     * Pointer to input data buffer or filename (if input_file is true).
     */
    uint8_t* input;
    /**
     * Size of the input data in bytes.
     */
    size_t size;
    /**
     * If true, 'input' is interpreted as a filename; if false, as a memory buffer.
     */
    bool input_file;
    /**
     * Unique job ID for tracking asynchronous processing.
     */
    uint32_t jobid;
    /**
     * If true, enables format extensions for this job.
     */
    bool use_extensions;
    /**
     * Compression level to use for this job.
     */
    uint32_t compression_level;
    /**
     * File stream for input, if applicable.
     */
    FILE* input_stream;
    /**
     * Size of the input data (may be redundant with 'size').
     */
    size_t input_size;
    /**
     * Start block to process
     */
    uint64_t start_block;
    /**
     * Number of blocks to process
     */
    uint64_t n_blocks;

    /**
     * Pointer to output data buffer or filename (if output_file is true).
     */
    uint8_t* output;
    /**
     * Size of the output data in bytes.
     */
    size_t outsize;
    /**
     * If true, 'output' is interpreted as a filename; if false, as a memory buffer.
     */
    bool output_file;
    /**
     * File stream for output, if applicable.
     */
    FILE* output_stream;
    /**
     * Flag indicating if an error occurred during processing.
     */
    bool error_occurred;

    /**
     * Callback function invoked upon job completion.
     * Signature: void(uint32_t jobid, bool success)
     */
    std::function<void(uint32_t jobid, bool)> completion_cb;
    /**
     * Callback function invoked to report progress.
     * Signature: void(uint32_t jobid, double progress) where progress is in [0.0, 1.0].
     */
    std::function<void(uint32_t jobid, double)> progress_cb;
};

/**
 * @class TSQCompressionContext_MT
 * @brief Multi-threaded compression context for parallel file or buffer compression.
 *
 * Manages worker threads, job queues, and synchronization for high-performance compression.
 *
 * Fields:
 *   num_cores - Number of worker threads (cores) used for compression.
 *   workers - Pointer to array of TSQWorker structures, one per thread.
 *   currentjob - Pointer to the current job being processed.
 *   threads - Array of thread pointers for worker threads.
 *   reader - Thread responsible for reading input data and dispatching jobs.
 *   writer - Thread responsible for writing output data.
 *   reader_mtx - Mutex for synchronizing access to the reader thread.
 *   reader_cv - Condition variable for reader thread coordination.
 *   blocks_writen - Number of blocks written so far (progress tracking).
 *   input_blocks - Total number of input blocks to process.
 *   queue - Pointer to the job queue for pending jobs.
 *   queue_mtx - Mutex for synchronizing access to the job queue.
 *   queue_cv - Condition variable for job queue coordination.
 *   maxjobid - Maximum job ID assigned so far.
 *   exit_request - If true, signals threads to exit.
 *   verbose - If true, enables verbose logging for debugging or progress reporting.
 */
class TSQCompressionContext_MT {
public:

    TSQCompressionContext_MT() : num_cores(1), workers(nullptr), threads(nullptr), reader(nullptr), writer(nullptr),
        reader_mtx(), reader_cv(), input_blocks(0), queue(nullptr), queue_mtx(), queue_cv(), maxjobid(1), req_mtx(), req_cv(),
        inflight_reqs(0), exit_request(false), verbose(false) {}

    uint32_t num_cores;
    struct TSQWorker* workers;

    std::thread** threads;
    std::thread* reader;
    std::thread* writer;

    std::mutex reader_mtx;
    std::condition_variable reader_cv;

    // scheduling
    uint64_t input_blocks;

    // Job queue
    std::queue<struct TSQJob*> *queue;
    std::mutex queue_mtx;
    std::condition_variable queue_cv;
    uint32_t maxjobid;
    std::mutex req_mtx;
    std::condition_variable req_cv;
    volatile int32_t inflight_reqs;

    // Exit
    bool exit_request;
    bool verbose;

};

/**
 * @class TSQDecompressionContext_MT
 * @brief Multi-threaded decompression context for parallel file or buffer decompression.
 *
 * Manages worker threads, job queues, and synchronization for high-performance decompression.
 *
 * Fields:
 *   num_cores - Number of worker threads (cores) used for decompression.
 *   workers - Pointer to array of TSQWorker structures, one per thread.
 *   currentjob - Pointer to the current job being processed.
 *   threads - Array of thread pointers for worker threads.
 *   reader - Thread responsible for reading input data and dispatching jobs.
 *   writer - Thread responsible for writing output data.
 *   reader_mtx - Mutex for synchronizing access to the reader thread.
 *   reader_cv - Condition variable for reader thread coordination.
 *   blocks_writen - Number of blocks written so far (progress tracking).
 *   input_blocks - Total number of input blocks to process.
 *   queue - Pointer to the job queue for pending jobs.
 *   queue_mtx - Mutex for synchronizing access to the job queue.
 *   queue_cv - Condition variable for job queue coordination.
 *   maxjobid - Maximum job ID assigned so far.
 *   exit_request - If true, signals threads to exit.
 *   verbose - If true, enables verbose logging for debugging or progress reporting.
 */
class TSQDecompressionContext_MT {
public:

    TSQDecompressionContext_MT() : num_cores(1), workers(nullptr), threads(nullptr), reader(nullptr), writer(nullptr),
        reader_mtx(), reader_cv(), input_blocks(0), queue(nullptr), queue_mtx(), queue_cv(), maxjobid(1), req_mtx(), req_cv(),
        inflight_reqs(0), exit_request(false), verbose(false) {}

    uint32_t num_cores;
    struct TSQWorker* workers;

    std::thread** threads;
    std::thread* reader;
    std::thread* writer;

    std::mutex reader_mtx;
    std::condition_variable reader_cv;

    // scheduling
    uint64_t input_blocks;
    uint64_t blocks_writen;

    // Job queue
    std::queue<struct TSQJob*> *queue;
    std::mutex queue_mtx;
    std::condition_variable queue_cv;
    uint32_t maxjobid;
    std::mutex req_mtx;
    std::condition_variable req_cv;
    volatile int32_t inflight_reqs;

    // Exit
    bool exit_request;
    bool verbose;

};


#if defined (__cplusplus)
extern "C" {
#endif

    /**
     * Compresses data from an input file stream and writes the compressed output to an output file stream.
     * This is a high-level, single-threaded API for file-to-file compression.
     *
     * @param in Input file stream to read uncompressed data from. Must be opened in binary mode.
     * @param out Output file stream to write compressed data to. Must be opened in binary mode.
     * @param useextensions If true, enables format extensions for improved compression or features.
     * @param level Compression level (implementation-defined, typically 0 = fastest, higher = better compression).
     *              Not all levels may be supported; see documentation for details.
     *
     * @note The function handles reading, compressing, and writing in blocks. It writes a TSQ1 header to the output.
     * @note Input and output streams must be valid and open. The function does not close the streams.
     */
    void compress( FILE* in, FILE* out, bool useextensions, uint32_t level );

    /**
     * Decompresses data from an input file stream and writes the decompressed output to an output file stream.
     * This is a high-level, single-threaded API for file-to-file decompression.
     *
     * @param in Input file stream to read compressed data from. Must be opened in binary mode.
     * @param out Output file stream to write decompressed data to. Must be opened in binary mode.
     *
     * @note The function expects a valid TSQ1 header in the input stream. It handles reading, decompressing, and writing in blocks.
     * @note Input and output streams must be valid and open. The function does not close the streams.
     */
    void decompress( FILE* in, FILE* out );

    /**
     * Allocates and initializes a multi-threaded compression context.
     *
     * @param verbose If true, enables verbose logging for debugging or progress reporting.
     * @return Pointer to a newly allocated TSQCompressionContext_MT structure, or nullptr on failure.
     *
     * @note The returned context must be deallocated with tsqDeallocateContextCompression_MT().
     */
    struct TSQCompressionContext_MT* tsqAllocateContextCompression_MT( bool verbose );

    /**
     * Deallocates a multi-threaded compression context and releases all associated resources.
     *
     * @param ctx Pointer to a TSQCompressionContext_MT previously allocated by tsqAllocateContextCompression_MT().
     *
     * @note After this call, the context pointer is invalid and must not be used.
     */
    void tsqDeallocateContextCompression_MT(struct TSQCompressionContext_MT* ctx);

    /**
     * Compresses data using a multi-threaded context.
     *
     * @param ctx Compression context allocated by tsqAllocateContextCompression_MT().
     * @param in Pointer to input data buffer or filename (see infile).
     * @param szin Size of the input data in bytes.
     * @param infile If true, 'in' is interpreted as a filename; if false, as a memory buffer.
     * @param out Pointer to a pointer that will receive the output buffer address (allocated by the function if outfile is false).
     * @param szout Pointer to a variable that will receive the size of the compressed output in bytes.
     * @param outfile If true, 'out' is interpreted as a filename; if false, as a memory buffer.
     * @param useextensions If true, enables format extensions for improved compression or features.
     * @param level Compression level (implementation-defined, typically 0 = fastest, higher = better compression).
     * @return True on success, false on failure.
     *
     * @note If outfile is false, the function allocates the output buffer, which must be freed by the caller using free().
     * @note Thread safety: the context should not be used concurrently by multiple threads.
     */
    bool tsqCompress_MT( TSQCompressionContext_MT* ctx, uint8_t* in, size_t szin, bool infile, uint8_t** out, size_t *szout, bool outfile, bool useextensions, uint32_t level );

    /**
     * Compresses data asynchronously using a multi-threaded context.
     *
     * This function schedules a compression job and returns immediately with a unique job ID.
     * The actual compression is performed in the background by worker threads.
     * Completion and progress are reported via user-provided callback functions.
     *
     * @param ctx Pointer to a TSQCompressionContext_MT structure, managing worker threads and job queues.
     * @param in Pointer to input data buffer or filename (see infile).
     *           If infile is true, this should be a filename string; otherwise, a memory buffer.
     * @param szin Size of the input data in bytes. Ignored if infile is true and input is a filename.
     * @param infile If true, 'in' is interpreted as a filename; if false, as a memory buffer.
     * @param out Pointer to a pointer that will receive the output buffer address (allocated by the function if outfile is false).
     *            If outfile is true, this should be a filename string; otherwise, a pointer to a memory buffer.
     * @param szout Pointer to a variable that will receive the size of the compressed output in bytes.
     * @param outfile If true, 'out' is interpreted as a filename; if false, as a memory buffer.
     * @param useextensions If true, enables format extensions for improved compression or features.
     * @param level Compression level (implementation-defined, typically 0 = fastest, higher = better compression).
     * @param user_completion_cb Optional user callback invoked when the job completes.
     *        Signature: void(uint32_t jobid, bool success)
     *        - jobid: Unique job identifier.
     *        - success: True if compression succeeded, false otherwise.
     * @param user_progress_cb Optional user callback invoked to report progress.
     *        Signature: void(uint32_t jobid, double progress)
     *        - jobid: Unique job identifier.
     *        - progress: Progress value in [0.0, 1.0].
     *
     * @return Unique job ID for the scheduled compression task. Use this ID to track job status in callbacks.
     *
     * @note The function returns immediately; compression occurs asynchronously.
     * @note If outfile is false, the output buffer is allocated and must be freed by the caller.
     * @note Thread safety: the context should not be used concurrently by multiple threads.
     */
    uint32_t tsqCompressAsync_MT( TSQCompressionContext_MT* ctx, uint8_t* in, size_t szin, bool infile, uint8_t** out, size_t *szout, bool outfile, bool useextensions, uint32_t level,
        std::function<void(uint32_t jobid, bool)> user_completion_cb, std::function<void(uint32_t jobid, double)> user_progress_cb );

    /**
     * Allocates and initializes a multi-threaded decompression context.
     *
     * @param verbose If true, enables verbose logging for debugging or progress reporting.
     * @return Pointer to a newly allocated TSQDecompressionContext_MT structure, or nullptr on failure.
     *
     * @note The returned context must be deallocated with tsqDeallocateContextDecompression_MT().
     */
    struct TSQDecompressionContext_MT* tsqAllocateContextDecompression_MT( bool verbose );

    /**
     * Deallocates a multi-threaded decompression context and releases all associated resources.
     *
     * @param ctx Pointer to a TSQDecompressionContext_MT previously allocated by tsqAllocateContextDecompression_MT().
     *
     * @note After this call, the context pointer is invalid and must not be used.
     */
    void tsqDeallocateContextDecompression_MT(struct TSQDecompressionContext_MT* ctx);

    /**
     * Decompresses data using a multi-threaded context.
     *
     * @param ctx Decompression context allocated by tsqAllocateContextDecompression_MT().
     * @param in Pointer to input data buffer or filename (see infile).
     * @param szin Size of the input data in bytes.
     * @param infile If true, 'in' is interpreted as a filename; if false, as a memory buffer.
     * @param out Pointer to a pointer that will receive the output buffer address (allocated by the function if outfile is false).
     * @param szout Pointer to a variable that will receive the size of the decompressed output in bytes.
     * @param outfile If true, 'out' is interpreted as a filename; if false, as a memory buffer.
     * @return True on success, false on failure.
     *
     * @note If outfile is false, the function allocates the output buffer, which must be freed by the caller using free().
     * @note Thread safety: the context should not be used concurrently by multiple threads.
     */
    bool tsqDecompress_MT( TSQDecompressionContext_MT* ctx, uint8_t* in, size_t szin, bool infile, uint8_t** out, size_t* szout, bool outfile );

    /**
     * Decompresses data asynchronously using a multi-threaded context.
     *
     * This function schedules a decompression job and returns immediately with a unique job ID.
     * The actual decompression is performed in the background by worker threads.
     * Completion and progress are reported via user-provided callback functions.
     *
     * @param ctx Pointer to a TSQDecompressionContext_MT structure, managing worker threads and job queues.
     * @param in Pointer to input data buffer or filename (see infile).
     *           If infile is true, this should be a filename string; otherwise, a memory buffer.
     * @param szin Size of the input data in bytes. Ignored if infile is true and input is a filename.
     * @param infile If true, 'in' is interpreted as a filename; if false, as a memory buffer.
     * @param out Pointer to a pointer that will receive the output buffer address (allocated by the function if outfile is false).
     *            If outfile is true, this should be a filename string; otherwise, a pointer to a memory buffer.
     * @param szout Pointer to a variable that will receive the size of the decompressed output in bytes.
     * @param outfile If true, 'out' is interpreted as a filename; if false, as a memory buffer.
     * @param user_completion_cb Optional user callback invoked when the job completes.
     *        Signature: void(uint32_t jobid, bool success, uint8_t* output, size_t sz)
     *        - jobid: Unique job identifier.
     *        - success: True if decompression succeeded, false otherwise.
     *        - output: Pointer to decompressed data (if outfile is false).
     *        - sz: Size of decompressed data.
     * @param user_progress_cb Optional user callback invoked to report progress.
     *        Signature: void(uint32_t jobid, double progress)
     *        - jobid: Unique job identifier.
     *        - progress: Progress value in [0.0, 1.0].
     *
     * @return Unique job ID for the scheduled decompression task. Use this ID to track job status in callbacks.
     *
     * @note The function returns immediately; decompression occurs asynchronously.
     * @note If outfile is false, the output buffer is allocated and must be freed by the caller.
     * @note Thread safety: the context should not be used concurrently by multiple threads.
     */
    uint32_t tsqDecompressAsync_MT( TSQDecompressionContext_MT* ctx, uint8_t* in, size_t szin, bool infile, uint8_t** out, size_t* szout, bool outfile,
        std::function<void(uint32_t jobid, bool)> user_completion_cb, std::function<void(uint32_t jobid, double)> user_progress_cb );

    /**
     * Allocates and initializes a low-level compression context for single-threaded use.
     *
     * @return Pointer to a newly allocated TSQCompressionContext structure, or nullptr on failure.
     *
     * @note The returned context must be deallocated with tsqDeallocateContext().
     */
    struct TSQCompressionContext* tsqAllocateContext();

    /**
     * Deallocates a low-level compression context and releases all associated resources.
     *
     * @param ctx Pointer to a TSQCompressionContext previously allocated by tsqAllocateContext().
     *
     * @note After this call, the context pointer is invalid and must not be used.
     */
    void tsqDeallocateContext(struct TSQCompressionContext* ctx);

    /**
     * Encodes (compresses) a single block of data using the provided compression context.
     *
     * @param ctx Compression context allocated by tsqAllocateContext().
     * @param inputBlock Pointer to the input data block to compress.
     * @param outputBlock Pointer to the output buffer to receive compressed data.
     * @param outputSize Pointer to a variable that will receive the size of the compressed output in bytes.
     * @param inputSize Size of the input data block in bytes.
     * @param withExtensions If true, enables format extensions for improved compression or features.
     *
     * @note The output buffer must be large enough to hold the compressed data (see TSQ_OUTPUT_SZ).
     */
    void tsqEncode( struct TSQCompressionContext* ctx, uint8_t *inputBlock, uint8_t *outputBlock, uint32_t *outputSize, uint32_t inputSize, uint32_t withExtensions );

    /**
     * Decodes (decompresses) a single block of data.
     *
     * @param inputBlock Pointer to the input data block to decompress.
     * @param outputBlock Pointer to the output buffer to receive decompressed data.
     * @param outputSize Pointer to a variable that will receive the size of the decompressed output in bytes.
     * @param inputSize Size of the input data block in bytes.
     * @param withExtensions If true, enables format extensions for improved decompression or features.
     *
     * @note The output buffer must be large enough to hold the decompressed data (see TSQ_BLOCK_SZ).
     */
    void tsqDecode( uint8_t *inputBlock, uint8_t *outputBlock, uint32_t *outputSize, uint32_t inputSize, uint32_t withExtensions );

#if defined (__cplusplus)
}
#endif

