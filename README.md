# Turbosqueeze

A multithreaded lossless compression library for C/C++.

Turbosqueeze splits input into independent blocks and processes them in parallel across worker threads, using a producer-consumer pipeline (reader → compressor workers → writer). It supports both synchronous and asynchronous APIs with progress/completion callbacks.

## Features

- **Multithreaded compression and decompression** with configurable thread count
- **Seven compression levels** (0–6): from fast hash-based encoding to optimal suffix-array parsing
- **Two stream formats**: TSQ1 (legacy) and TSQ2 (current)
- **File and in-memory buffer** support for both input and output
- **Asynchronous API** with job IDs, completion callbacks, and progress reporting
- **MIT licensed**

## Building

```bash
git clone https://github.com/julienperriercornet/turbosqueeze.git
cd turbosqueeze
mkdir build && cd build
cmake ..
make
```

To build with AddressSanitizer:

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
make
```

To run the tests:

```bash
ctest
```

## Command-line tool

The `tsq` sample binary supports compression, decompression, and benchmarking:

```bash
# Compress (level 0 = fast, level 6 = optimal)
./tsq -c input.dat output.tsq
./tsq -c:6 input.dat output.tsq

# Decompress
./tsq -d output.tsq recovered.dat

# Benchmark (requires enwik9 in working directory)
./tsq -b
```

## API usage

### Multithreaded file compression

```cpp
#include "turbosqueeze.h"

// Allocate a compression context with 8 worker threads
TSQCompressionContext_MT* ctx = tsqAllocateContextCompression_MT(8, false);

const char* inpath = "input.dat";
const char* outpath = "output.tsq";
size_t outsize = 0;

// Compress file-to-file, TSQ2 format, level 0 (fast)
tsqCompress_MT(ctx,
    (uint8_t*) inpath, 0, true,          // input: filename
    (uint8_t**) &outpath, &outsize, true, // output: filename
    2, 0);                                // version 2, level 0

tsqDeallocateContextCompression_MT(ctx);
```

### Multithreaded file decompression

```cpp
#include "turbosqueeze.h"

TSQDecompressionContext_MT* ctx = tsqAllocateContextDecompression_MT(8, false);

const char* inpath = "output.tsq";
const char* outpath = "recovered.dat";
size_t outsize = 0;

tsqDecompress_MT(ctx,
    (uint8_t*) inpath, 0, true,
    (uint8_t**) &outpath, &outsize, true);

tsqDeallocateContextDecompression_MT(ctx);
```

### In-memory compression and decompression

```cpp
#include "turbosqueeze.h"
#include <cstdlib>
#include <cstring>

const char* data = "Data to compress...";
size_t data_len = strlen(data);

// Compress
TSQCompressionContext_MT* cctx = tsqAllocateContextCompression_MT(8, false);
uint8_t* compressed = nullptr;
size_t compressed_sz = 0;

tsqCompress_MT(cctx,
    (uint8_t*) data, data_len, false,       // input: memory buffer
    &compressed, &compressed_sz, false,      // output: allocated by library
    2, 0);

tsqDeallocateContextCompression_MT(cctx);

// Decompress
TSQDecompressionContext_MT* dctx = tsqAllocateContextDecompression_MT(8, false);
uint8_t* decompressed = nullptr;
size_t decompressed_sz = 0;

tsqDecompress_MT(dctx,
    compressed, compressed_sz, false,
    &decompressed, &decompressed_sz, false);

tsqDeallocateContextDecompression_MT(dctx);

// decompressed now contains the original data
free(compressed);
free(decompressed);
```

### Asynchronous compression with callbacks

```cpp
#include "turbosqueeze.h"

TSQCompressionContext_MT* ctx = tsqAllocateContextCompression_MT(8, false);
uint8_t* compressed = nullptr;
size_t compressed_sz = 0;

uint32_t jobid = tsqCompressAsync_MT(ctx,
    (uint8_t*) data, data_len, false,
    &compressed, &compressed_sz, false,
    2, 0,
    [](uint32_t jobid, bool success) {
        // Called when compression finishes
    },
    [](uint32_t jobid, double progress) {
        // Called periodically with progress in [0.0, 1.0]
    });

// Deallocate waits for all in-flight jobs to complete
tsqDeallocateContextCompression_MT(ctx);
free(compressed);
```

## Compression levels

| Level | Algorithm | Description |
|-------|-----------|-------------|
| 0 | Hash-based | Single-slot hash table, fastest |
| 1–5 | History-based | Multi-slot hash with increasing depth |
| 6 | Suffix-array | Optimal parsing, best ratio |

## API reference

See [turbosqueeze.h](./turbosqueeze.h) for the full API documentation.

## License

MIT. See [LICENSE](./LICENSE).
