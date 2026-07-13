#pragma once

/*
 * Turbosqueeze API.
 *
 * Copyright (c) 2024-2026 Julien Perrier-cornet
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
#include <stack>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>


#define TSQ_BLOCK_BITS (22)
#define TSQ_BLOCK_SZ (1<<TSQ_BLOCK_BITS)
#define TSQ_OUTPUT_SZ ((1<<TSQ_BLOCK_BITS) + (1<<(TSQ_BLOCK_BITS-2)))

#define TSQ_HASH_BITS (17)
#define TSQ_HASH_SZ ((1<<TSQ_HASH_BITS) * sizeof(uint16_t))
#define TSQ_HASH_MASK ((1<<TSQ_HASH_BITS) - 1)

#define TSQ_HASH_HIST_BITS (19)
#define TSQ_HASH_HIST_SZ ((1<<TSQ_HASH_BITS) * sizeof(uint16_t))
#define TSQ_HASH_HIST_MASK ((1<<TSQ_HASH_BITS) - 1)

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

struct TSQCompressionContext3 {
    /** Hash table for fast pattern matching during compression. */
    uint32_t *refhash;
};

struct TSQDecompressionContext3 {
    // Single backing allocation; sub-buffers point into it.
    uint8_t *buffer;
    // Decoded stream sub-buffers
    uint8_t *sizeBuffer;
    uint8_t *offsetHighBuffer;
    uint8_t *literalsBuffer;
    uint8_t *extraLitSizeBuffer;
    uint8_t *extraRepSizeBuffer;
    uint8_t *offsetLowBuffer;
    // End offsets written by tsqReadStream3
    uint32_t sizeBufferEnd;
    uint32_t offsetHighBufferEnd;
    uint32_t literalsBufferEnd;
    uint32_t extraLitSizeBufferEnd;
    uint32_t extraRepSizeBufferEnd;
    uint32_t offsetLowBufferEnd;
};

/**
 * @struct TSQCompressionContextHist
 * @brief Low-level compression context with history-based matching for single-threaded use.
 *
 * Extends the basic hash-based approach with a sliding history window per hash slot,
 * allowing the encoder to evaluate multiple match candidates and select the best one.
 * Used by compression levels 1 through 5.
 */
struct TSQCompressionContextHist {
    /**
     * Pointer to the reference hash table mapping 4-byte patterns to buffer positions.
     * Each slot stores the most recent position that hashed to that slot.
     */
    uint16_t *refhash;
    /**
     * Number of history entries per hash slot. Derived from the compression level:
     * histent = 1 << (1 + level). Higher values increase match quality at the cost of speed.
     */
    uint32_t histent;
    /**
     * Pointer to the count array tracking the cycling write index for each hash slot.
     * Used to implement circular replacement within the history ring buffer.
     */
    uint32_t *cnthash;
};



/**
 * @class TSQOptContext
 * @brief Compression context for the optimal encoder (compression level 6).
 *
 * Uses a suffix-array approach: all rotations of the input block are sorted
 * lexicographically, then a backward greedy LZ parser walks the sorted array
 * to find the best matches. The resulting token sequence (literals and matches)
 * is stored on a stack and later serialized into the TSQ2 compressed format.
 */
class TSQOptContext {
public:

    /**
     * @class TSQData
     * @brief Represents a single token emitted by the backward greedy LZ parser.
     *
     * Each token is either a literal run or an LZ match reference.
     * Tokens are pushed onto the path stack in reverse order and popped
     * sequentially during output serialization.
     */
    class TSQData {
    public:
        /** Start position of this token in the original input buffer. */
        uint32_t pos;
        /** For matches (type==1): the source position that this match copies from.
         *  Unused for literals (type==2). */
        uint32_t hitpos;
        /** Length of this token in bytes. For matches, must be >= 4. */
        uint16_t len;
        /** Token type: 1 = LZ match, 2 = literal run. */
        uint8_t type;

        TSQData() : pos(0), hitpos(0), len(0), type(0) {}
        TSQData(const TSQData &other) : pos(other.pos), hitpos(other.hitpos), len(other.len), type(other.type) {}
    };

    /**
     * Sorted rotation indices. After sorting, sorthits[i] contains the buffer
     * position whose rotation ranks i-th lexicographically. Size: inputSize entries.
     */
    uint32_t** sorthits;
    /**
     * Inverse mapping of sorthits. reverse_sorthits[pos] gives the rank of the
     * rotation starting at position pos. Used to locate a position's neighborhood
     * in the sorted array for fast LZ match search.
     */
    uint32_t** reverse_sorthits;
    /**
     * Stack of encoding tokens built by the backward greedy parser.
     * Tokens are pushed back-to-front and popped front-to-back during output.
     */
    std::stack<TSQData> path;

    TSQOptContext() : sorthits(nullptr), reverse_sorthits(nullptr), path() {}
    TSQOptContext(const TSQOptContext &other) : sorthits(other.sorthits), reverse_sorthits(other.reverse_sorthits), path(other.path) {}

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
 *   job - Pointer to the associated TSQJob for this buffer.
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
     * Pointer to the job associated with this buffer.
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

    /**
     * Stream format version for this buffer's block:
     * 1 = TSQ1 (legacy format with extensions support),
     * 2 = TSQ2 (current format, supports multiple compression levels).
     */
    uint32_t version;
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
 *   currentReadInput - Index of the next input buffer to be read (64-bit).
 *   currentWorkInput - Index of the input buffer currently being processed (64-bit).
 *   input_mtx - Mutex for synchronizing access to input buffers.
 *   input_cv - Condition variable for input buffer availability.
 *   outputs - Vector of output buffers produced by the worker.
 *   n_outputs - Number of output buffers assigned to this worker.
 *   currentWorkOutput - Index of the output buffer currently being written (64-bit).
 *   currentWriteOutput - Index of the next output buffer to be written to disk or memory (64-bit).
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
     * Index of the next input buffer to be read by the worker (64-bit).
     * Volatile for safe concurrent access.
     */
    std::atomic<uint64_t> currentReadInput;
    /**
     * Index of the input buffer currently being processed (64-bit).
     * Volatile for safe concurrent access.
     */
    std::atomic<uint64_t> currentWorkInput;
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
     * Index of the output buffer currently being written (64-bit).
     * Volatile for safe concurrent access.
     */
    std::atomic<uint64_t> currentWorkOutput;
    /**
     * Index of the next output buffer to be written to disk or memory (64-bit).
     * Volatile for safe concurrent access.
     */
    std::atomic<uint64_t> currentWriteOutput;
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
 * \class TSQJob
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
 *   start_block - Index of the first block to process (for partial jobs).
 *   n_blocks - Number of blocks to process (for partial jobs).
 *   output - Pointer to output data buffer or filename (if output_file is true).
 *   outsize - Size of the output data in bytes.
 *   output_file - If true, 'output' is a filename; if false, a memory buffer.
 *   output_stream - File stream for output, if applicable.
 *   error_occurred - Flag indicating if an error occurred during processing.
 *   completion_cb - Callback function invoked upon job completion (jobid, success).
 *   progress_cb - Callback function invoked to report progress (jobid, progress [0.0-1.0]).
 */
class TSQJob {
public:

    /**
     * Constructor initializes members to default values.
     */
    TSQJob() : input(nullptr), size(0), input_file(false), jobid(0), use_extensions(false), compression_level(0), compression_method(2), input_stream(nullptr),
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
    uint64_t size;
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
     * Stream format version / compression method for this job:
     * 1 = TSQ1 (legacy format with extension support),
     * 2 = TSQ2 (current format, supports levels 0-6).
     * Written into the stream header and propagated to worker buffers.
     */
    uint32_t compression_method;
    /**
     * File stream for input, if applicable.
     */
    FILE* input_stream;
    /**
     * Size of the input data (may be redundant with 'size').
     */
    uint64_t input_size;
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
    uint64_t outsize;
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
 * Orchestrates a pipeline of reader, compression workers, and writer threads.
 * The reader thread reads input blocks from files or memory and dispatches them
 * to worker threads via TSQBuffer queues. Each worker compresses its block
 * independently. The writer thread collects compressed blocks in order and
 * writes them to the output file or buffer.
 *
 * Lifecycle: allocate with tsqAllocateContextCompression_MT(), submit one or
 * more jobs with tsqCompress_MT() or tsqCompressAsync_MT(), then deallocate
 * with tsqDeallocateContextCompression_MT().
 */
class TSQCompressionContext_MT {
public:

    TSQCompressionContext_MT() : num_cores(1), workers(nullptr), threads(nullptr), reader(nullptr), writer(nullptr),
        reader_mtx(), reader_cv(), input_blocks(0), queue(nullptr), queue_mtx(), queue_cv(), maxjobid(1), req_mtx(), req_cv(),
        inflight_reqs(0), exit_request(false), verbose(false) {}

    /** Number of compression worker threads. Set at allocation time. */
    uint32_t num_cores;
    /** Array of num_cores TSQWorker structures, each owning input/output buffer queues. */
    struct TSQWorker* workers;

    /** Array of num_cores std::thread pointers, one per compression worker. */
    std::thread** threads;
    /** Reader thread that reads input data and distributes blocks to workers round-robin. */
    std::thread* reader;
    /** Writer thread that collects compressed blocks from workers and writes output in order. */
    std::thread* writer;

    /** Mutex protecting the reader thread's wait condition (worker input capacity). */
    std::mutex reader_mtx;
    /** Condition variable signalled when a worker frees an input slot. */
    std::condition_variable reader_cv;

    /** Running count of input blocks dispatched across all jobs since context creation. */
    uint64_t input_blocks;

    /** FIFO queue of pending TSQJob pointers waiting to be read and dispatched. */
    std::queue<struct TSQJob*> *queue;
    /** Mutex protecting the job queue. */
    std::mutex queue_mtx;
    /** Condition variable signalled when a new job is enqueued. */
    std::condition_variable queue_cv;
    /** Next job ID to assign. Monotonically increasing starting from 1. */
    uint32_t maxjobid;
    /** Mutex protecting the inflight request counter. */
    std::mutex req_mtx;
    /** Condition variable signalled when a new request arrives or completes. */
    std::condition_variable req_cv;
    /** Number of jobs currently in-flight (submitted but not yet fully written). */
    std::atomic<int32_t> inflight_reqs;

    /** When set to true, all threads drain their queues and exit. */
    std::atomic<bool> exit_request;
    /** If true, compression progress and completion messages are printed to stdout. */
    bool verbose;

};

/**
 * @class TSQDecompressionContext_MT
 * @brief Multi-threaded decompression context for parallel file or buffer decompression.
 *
 * Mirrors TSQCompressionContext_MT but for the decompression pipeline.
 * The reader thread parses the stream header, reads compressed blocks, and
 * dispatches them to worker threads. Each worker decompresses its block.
 * The writer thread collects decompressed blocks in order and writes them out.
 *
 * Lifecycle: allocate with tsqAllocateContextDecompression_MT(), submit one or
 * more jobs with tsqDecompress_MT() or tsqDecompressAsync_MT(), then deallocate
 * with tsqDeallocateContextDecompression_MT().
 */
class TSQDecompressionContext_MT {
public:

    TSQDecompressionContext_MT() : num_cores(1), workers(nullptr), threads(nullptr), reader(nullptr), writer(nullptr),
        reader_mtx(), reader_cv(), input_blocks(0), queue(nullptr), queue_mtx(), queue_cv(), maxjobid(1), req_mtx(), req_cv(),
        inflight_reqs(0), exit_request(false), verbose(false) {}

    /** Number of decompression worker threads. Set at allocation time. */
    uint32_t num_cores;
    /** Array of num_cores TSQWorker structures, each owning input/output buffer queues. */
    struct TSQWorker* workers;

    /** Array of num_cores std::thread pointers, one per decompression worker. */
    std::thread** threads;
    /** Reader thread that parses the compressed stream and distributes blocks to workers. */
    std::thread* reader;
    /** Writer thread that collects decompressed blocks from workers and writes output in order. */
    std::thread* writer;

    /** Mutex protecting the reader thread's wait condition (worker input capacity). */
    std::mutex reader_mtx;
    /** Condition variable signalled when a worker frees an input slot. */
    std::condition_variable reader_cv;

    /** Running count of compressed input blocks dispatched across all jobs. */
    uint64_t input_blocks;
    /** Running count of decompressed blocks written out. Used for progress tracking. */
    uint64_t blocks_writen;

    /** FIFO queue of pending TSQJob pointers waiting to be read and dispatched. */
    std::queue<struct TSQJob*> *queue;
    /** Mutex protecting the job queue. */
    std::mutex queue_mtx;
    /** Condition variable signalled when a new job is enqueued. */
    std::condition_variable queue_cv;
    /** Next job ID to assign. Monotonically increasing starting from 1. */
    uint32_t maxjobid;
    /** Mutex protecting the inflight request counter. */
    std::mutex req_mtx;
    /** Condition variable signalled when a new request arrives or completes. */
    std::condition_variable req_cv;
    /** Number of jobs currently in-flight (submitted but not yet fully written). */
    std::atomic<int32_t> inflight_reqs;

    /** When set to true, all threads drain their queues and exit. */
    std::atomic<bool> exit_request;
    /** If true, decompression progress and completion messages are printed to stdout. */
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
    void tsqCompress( FILE* in, FILE* out, bool useextensions, uint32_t level );

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
    void tsqDecompress( FILE* in, FILE* out );

    /**
     * Allocates and initializes a multi-threaded compression context.
     *
     * @param n_threads Number of threads to use for compression. If 0, the compression will use 1 thread.
     * @param verbose If true, enables verbose logging for debugging or progress reporting.
     * @return Pointer to a newly allocated TSQCompressionContext_MT structure, or nullptr on failure.
     *
     * @note The returned context must be deallocated with tsqDeallocateContextCompression_MT().
     */
    struct TSQCompressionContext_MT* tsqAllocateContextCompression_MT( uint32_t n_threads, bool verbose );

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
     * @param in Pointer to input data buffer, or a null-terminated filename string if infile is true.
     * @param szin Size of the input data in bytes. When infile is true, the actual size is read from the file.
     * @param infile If true, 'in' is interpreted as a filename to open; if false, as an in-memory buffer.
     * @param out Pointer to a pointer that receives the output. When outfile is true, points to a filename
     *            string; when false, the function allocates the output buffer and stores its address here.
     * @param szout Pointer to a variable that receives the compressed output size in bytes.
     * @param outfile If true, compressed data is written to the file named by *out; if false, to a malloc'd buffer.
     * @param version Stream format version: 1 = TSQ1 (legacy), 2 = TSQ2 (current). Determines the stream
     *                header written and the block framing used.
     * @param level Compression level controlling the encoder algorithm:
     *              0 = fast hash-based encoder, 1-5 = history-based encoder (higher = more candidates),
     *              6 = optimal suffix-array encoder.
     * @return True on success, false on failure (invalid parameters, I/O error, allocation failure).
     *
     * @note If outfile is false, the caller must free() the output buffer after use.
     * @note This call blocks until compression completes. For non-blocking operation, use tsqCompressAsync_MT().
     */
    bool tsqCompress_MT( TSQCompressionContext_MT* ctx, uint8_t* in, size_t szin, bool infile, uint8_t** out, size_t *szout, bool outfile, uint32_t version, uint32_t level );

    /**
     * Compresses data asynchronously using a multi-threaded context.
     *
     * This function schedules a compression job and returns immediately with a unique job ID.
     * The actual compression is performed in the background by worker threads.
     * Completion and progress are reported via user-provided callback functions.
     *
     * @param ctx Pointer to a TSQCompressionContext_MT structure, managing worker threads and job queues.
     * @param in Pointer to input data buffer, or a null-terminated filename string if infile is true.
     * @param szin Size of the input data in bytes. When infile is true, the actual size is determined from the file.
     * @param infile If true, 'in' is interpreted as a filename to open; if false, as an in-memory buffer.
     * @param out Pointer to a pointer that receives the output. When outfile is true, points to a filename
     *            string; when false, the function allocates the output buffer and stores its address here.
     * @param szout Pointer to a variable that receives the compressed output size in bytes.
     * @param outfile If true, compressed data is written to the file named by *out; if false, to a malloc'd buffer.
     * @param version Stream format version: 1 = TSQ1 (legacy), 2 = TSQ2 (current). Determines the stream
     *                header and block framing.
     * @param level Compression level controlling the encoder algorithm:
     *              0 = fast hash-based encoder, 1-5 = history-based encoder (higher = more candidates),
     *              6 = optimal suffix-array encoder.
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
    uint32_t tsqCompressAsync_MT( TSQCompressionContext_MT* ctx, uint8_t* in, size_t szin, bool infile, uint8_t** out, size_t *szout, bool outfile, uint32_t version, uint32_t level,
        std::function<void(uint32_t jobid, bool)> user_completion_cb, std::function<void(uint32_t jobid, double)> user_progress_cb );

    /**
     * Allocates and initializes a multi-threaded decompression context.
     *
     * @param n_threads Number of worker threads. Use 0 to auto-detect the hardware concurrency.
     * @param verbose If true, enables verbose logging for debugging or progress reporting.
     * @return Pointer to a newly allocated TSQDecompressionContext_MT structure, or nullptr on failure.
     *
     * @note The returned context must be deallocated with tsqDeallocateContextDecompression_MT().
     */
    struct TSQDecompressionContext_MT* tsqAllocateContextDecompression_MT( uint32_t n_threads, bool verbose );

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
     * @param in Pointer to compressed data buffer, or a null-terminated filename string if infile is true.
     * @param szin Size of the compressed input data in bytes. When infile is true, the size is read from the file.
     * @param infile If true, 'in' is interpreted as a filename to open; if false, as an in-memory buffer.
     * @param out Pointer to a pointer that receives the output. When outfile is true, points to a filename
     *            string; when false, the function allocates the output buffer and stores its address here.
     * @param szout Pointer to a variable that receives the decompressed output size in bytes.
     * @param outfile If true, decompressed data is written to the file named by *out; if false, to a malloc'd buffer.
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
    struct TSQCompressionContext3* tsqAllocateContext3Compression();
    struct TSQDecompressionContext3* tsqAllocateContext3();

    /**
     * Allocates and initializes a history-based compression context for single-threaded use.
     *
     * The history depth scales with level: histent = 1 << (1 + level).
     * Higher levels evaluate more match candidates per hash slot, improving compression
     * ratio at the cost of speed.
     *
     * @param level Compression level (1-5). Controls the number of history entries per hash slot.
     * @return Pointer to a newly allocated TSQCompressionContextHist, or nullptr on failure.
     *
     * @note The returned context must be deallocated with tsqDeallocateContextHist().
     */
    struct TSQCompressionContextHist* tsqAllocateContextHist( uint32_t level );

    /**
     * Allocates and initializes an optimal encoder context for single-threaded use.
     *
     * Allocates the suffix array (sorthits) and its inverse (reverse_sorthits),
     * each sized for one TSQ_BLOCK_SZ block.
     *
     * @return Pointer to a newly allocated TSQOptContext, or nullptr on failure.
     *
     * @note The returned context must be deallocated with tsqDeallocateContextOpt().
     */
    TSQOptContext* tsqAllocateContextOpt();

    /**
     * Deallocates a low-level compression context and releases all associated resources.
     *
     * @param ctx Pointer to a TSQCompressionContext previously allocated by tsqAllocateContext().
     *
     * @note After this call, the context pointer is invalid and must not be used.
     */
    void tsqDeallocateContext(struct TSQCompressionContext* ctx);
    void tsqDeallocateContext3(struct TSQDecompressionContext3* ctx);

    /**
     * Deallocates a history-based compression context and releases all associated resources.
     *
     * @param ctx Pointer to a TSQCompressionContextHist previously allocated by tsqAllocateContextHist().
     *
     * @note After this call, the context pointer is invalid and must not be used.
     */
    void tsqDeallocateContextHist(struct TSQCompressionContextHist* ctx);

    /**
     * Deallocates an optimal encoder context and releases the suffix arrays.
     *
     * @param ctx Pointer to a TSQOptContext previously allocated by tsqAllocateContextOpt().
     *
     * @note After this call, the context pointer is invalid and must not be used.
     */
    void tsqDeallocateContextOpt(TSQOptContext* ctx);

    /**
     * Initializes a TSQCompressionContext for block-based compression.
     *
     * @param ctx Pointer to a TSQCompressionContext structure to initialize.
     *
     * @note This function must be called before using the context for compression.
     */
    void tsqInit( struct TSQCompressionContext* ctx );
    void tsqInit3Compression( struct TSQCompressionContext3* ctx );
    void tsqInit3( struct TSQDecompressionContext3* ctx );

    /**
     * Resets a TSQCompressionContextHist for a new block.
     *
     * Zeroes the hash table and count array so the context can be reused for
     * the next input block. Must be called before each tsqEncode2_hist() call.
     *
     * @param ctx Pointer to a TSQCompressionContextHist to reinitialize.
     */
    void tsqInitHist( struct TSQCompressionContextHist* ctx, uint32_t level );

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
     * TSQ2 fast encoder (compression level 0).
     *
     * Uses a single-slot hash table for O(1) match lookup per position.
     * Fastest encoder with reasonable compression ratio. Produces the TSQ2
     * block format (4-bit nibble control + LZ tokens with 16-bit offsets).
     *
     * @param ctx Fast compression context allocated by tsqAllocateContext().
     *            Must be initialized with tsqInit() before each call.
     * @param input Pointer to the uncompressed input block. Must contain at least inputSize readable bytes.
     * @param output Pointer to the output buffer. Must be at least TSQ_OUTPUT_SZ bytes.
     * @param outputSize Receives the compressed size in bytes.
     * @param inputSize Size of the input block in bytes (at most TSQ_BLOCK_SZ).
     */
    void tsqEncode2_fast( struct TSQCompressionContext* ctx, uint8_t *input, uint8_t *output, uint32_t *outputSize, uint32_t inputSize );

    /**
     * TSQ2 history-based encoder (compression levels 1-5).
     *
     * Uses a multi-slot hash table where each slot stores a ring buffer
     * of recent positions (depth controlled by the context's histent).
     * Evaluates all candidates in each slot, selecting the longest match
     * with the smallest offset. Better ratio than the fast encoder at
     * moderate speed cost.
     *
     * @param ctx History compression context allocated by tsqAllocateContextHist(level).
     *            Must be initialized with tsqInitHist() before each call.
     * @param input Pointer to the uncompressed input block.
     * @param output Pointer to the output buffer. Must be at least TSQ_OUTPUT_SZ bytes.
     * @param outputSize Receives the compressed size in bytes.
     * @param inputSize Size of the input block in bytes (at most TSQ_BLOCK_SZ).
     */
    void tsqEncode2_hist( struct TSQCompressionContextHist* ctx, uint8_t *input, uint8_t *output, uint32_t *outputSize, uint32_t inputSize );

    /**
     * TSQ2 optimal encoder (compression level 6).
     *
     * Builds a suffix array of all input rotations, then uses a backward
     * greedy LZ parser that searches sorted neighbors for optimal matches.
     * Produces the best compression ratio at significant CPU cost.
     * The context's path stack is populated and then drained to emit output.
     *
     * @param ctx Optimal encoder context allocated by tsqAllocateContextOpt().
     *            No initialization call is needed between blocks; the suffix
     *            array and path stack are rebuilt internally for each call.
     * @param input Pointer to the uncompressed input block.
     * @param output Pointer to the output buffer. Must be at least TSQ_OUTPUT_SZ bytes.
     * @param outputSize Receives the compressed size in bytes. May be nullptr if the
     *                   caller does not need the size (e.g., during testing).
     * @param inputSize Size of the input block in bytes (at most TSQ_BLOCK_SZ).
     */
    void tsqEncode2_opt( TSQOptContext* ctx, uint8_t *input, uint8_t *output, uint32_t *outputSize, uint32_t inputSize );


    void tsqEncode3_fast( struct TSQCompressionContext3* ctx, uint8_t *input, uint8_t *output, uint32_t *outputSize, uint32_t inputSize );
    void tsqEncode3_hist( struct TSQCompressionContextHist* ctx, uint8_t *input, uint8_t *output, uint32_t *outputSize, uint32_t inputSize, uint8_t entropyCoding );
    void tsqEncode3_opt( TSQOptContext* ctx, uint8_t *input, uint8_t *output, uint32_t *outputSize, uint32_t inputSize, uint8_t entropyCoding );


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

    /**
     * Decodes (decompresses) a single TSQ2 compressed block.
     *
     * Reads the 6-byte block header (3 bytes uncompressed size + 3 bytes symbol count),
     * then processes the nibble-packed control stream to reconstruct the original data
     * from literal copies and 16-bit-offset LZ match references.
     *
     * @param inputBlock Pointer to the compressed block (header + control stream + payload).
     * @param outputBlock Pointer to the output buffer for decompressed data.
     *                    Must be at least TSQ_BLOCK_SZ bytes.
     * @param outputSize Receives the decompressed size in bytes (read from the block header).
     * @param inputSize Size of the compressed block in bytes.
     */
    void tsqDecode2( uint8_t *inputBlock, uint8_t *outputBlock, uint32_t *outputSize, uint32_t inputSize );

    void tsqDecode3( struct TSQDecompressionContext3* ctx, uint8_t *inputBlock, uint8_t *outputBlock, uint32_t *outputSize, uint32_t inputSize );


#if defined (__cplusplus)
}
#endif

