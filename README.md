# Turbosqueeze

**Realtime Multithreaded Compression Library for C/C++**

---

Turbosqueeze is a cutting-edge, high-performance compression library designed for modern C and C++ applications. Leveraging advanced multithreading and efficient algorithms, Turbosqueeze delivers lightning-fast data compression and decompression, making it ideal for demanding workloads, large files, and real-time systems.

## 🚀 Why Turbosqueeze?

- **Blazing Fast Performance:**
  - Multicore parallelism ensures minimal waiting times and maximum throughput.
  - Optimized for both file and memory buffer operations.
- **Seamless User Experience:**
  - Reduced loading times for applications and games.
  - Instantaneous data access for end-users.
- **Energy Efficient & Green Computing:**
  - Less CPU time means lower power consumption.
  - Efficient resource usage helps reduce heat and extend device battery life.
  - By minimizing computational waste, Turbosqueeze contributes to a greener, more sustainable digital ecosystem.
- **Scalable & Flexible:**
  - Easily integrates into existing projects.
  - Supports both synchronous and asynchronous workflows.
  - Handles massive datasets with ease.

## 🌱 Contributing to a Greener Future

High-performance libraries like Turbosqueeze do more than just speed up your software—they help the planet:

- **Lower Energy Footprint:**
  - Fast algorithms mean less time spent running on hardware, reducing overall energy usage.
- **Reduced Carbon Emissions:**
  - Efficient code means data centers and personal devices consume less electricity, helping lower global emissions.
- **Better for Daily Life:**
  - Faster apps and services mean less waiting, less frustration, and more productivity for everyone.

## ✨ Features

- Realtime, multithreaded compression and decompression
- File and memory buffer support
- Asynchronous job scheduling with progress and completion callbacks
- Highly configurable compression levels and format extensions
- MIT licensed for maximum freedom

## 📦 Getting Started

1. **Clone the repository:**
   ```bash
   git clone https://github.com/julienperriercornet/turbosqueeze.git
   cd turbosqueeze
   ```
2. **Build with CMake:**
   ```bash
   mkdir build && cd build
   cmake ..
   make
   ```
3. **Integrate into your project:**
   - Include `turbosqueeze.h` in your source files.
   - Link against the generated library.

## 🛠 Example Usage

```cpp
#include "turbosqueeze.h"

// Compress a file
FILE* in = fopen("input.dat", "rb");
FILE* out = fopen("output.tsq", "wb");
tsqCompress(in, out, true, 0); // Use extensions, fast compression
fclose(in);
fclose(out);
```

## ⚡ Benchmarks — Single-threaded Performance (enwik9, 1GB)

The following single-threaded benchmark was produced with lzbench on the enwik9 dataset (1,000,000,000 bytes). Results show compression and decompression throughput (MB/s), compressed output size and compression ratio.


lzbench 2.1 | GCC 14.2.0 | 64-bit Linux | AMD Ryzen 7 3700U with Radeon Vega Mobile Gfx  


| Compressor | Compression (MB/s) | Decompression (MB/s) | Compressed size (bytes) | Ratio (%) |
|---|---:|---:|---:|---:|
| memcpy | 12986 | 13047 | 1,000,000,000 | 100.00 |
| lz4 (1.10.0) | 439 | 3247 | 509,453,867 | 50.95 |
| lizard (2.1) -10 | 390 | 2095 | 509,932,407 | 50.99 |
| snappy (1.2.1) | 299 | 822 | 502,357,470 | 50.24 |
| fastlz (0.5.0) -1 | 231 | 563 | 504,940,620 | 50.49 |
| zstd (1.5.7) -1 | 368 | 1226 | 357,148,281 | 35.71 |
| zstd (1.5.7) -3 | 200 | 1014 | 313,354,892 | 31.34 |
| zstd (1.5.7) -5 | 91.8 | 953 | 298,064,058 | 29.81 |
| turbosqueeze (1.0) --no-ext | 305 | 2503 | 622,534,840 | 62.25 |

Notes:

- Dataset: enwik9 (a 1 GB text corpus commonly used for compression benchmarks).
- Measurements are single-threaded and reflect the raw throughput on the test machine (see bench.txt for full environment).
- `memcpy` is included as a theoretical maximum baseline for memory bandwidth.

Interpretation:

- Turbosqueeze provides a strong balance between throughput and decompression speed: it achieves competitive compression throughput (305 MB/s) and very high decompression speed (2,503 MB/s) on this dataset.
- For scenarios where minimal storage size is the top priority, higher compression settings of zstd or other higher-compression algorithms are preferable; for realtime streaming, Turbosqueeze and LZ4-family compressors offer excellent tradeoffs.
- This is for a fair and square comparaison of turbosqueeze versus other fast loseless compressors in single threaded performance, but the point of turbosqueeze is the multithreaded compression and decompression. The command `./tsq b` is offering a multithreaded benchmark using enwik9 as a test file, and acheives 1813 MB/s compression speed and 5818 MB/s decompression speed. 

Reproducing the benchmark:

- The original results were generated with `lzbench` (see `bench.txt` in this repository). To reproduce locally:
  1. Install lzbench (https://github.com/inikep/lzbench) and the compressors you want to test.
  2. Run: `./lzbench -b4096 -t3,5 enwik9` (adjust flags to your desired test configuration).

For full raw output and environment details, see `bench.txt` in the repository root.

## 📚 Documentation

See the [API documentation](./turbosqueeze.h) for details on all functions, structures, and usage patterns.

## 🤝 License

Turbosqueeze is released under the MIT License. See [LICENSE](./LICENSE) for details.

---

**Turbosqueeze: Fast, efficient, and eco-friendly compression for the future.**
