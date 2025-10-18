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
compress(in, out, true, 9); // Use extensions, max compression
fclose(in);
fclose(out);
```

## 📚 Documentation

See the [API documentation](./turbosqueeze.h) for details on all functions, structures, and usage patterns.

## 🤝 License

Turbosqueeze is released under the MIT License. See [LICENSE](./LICENSE) for details.

---

**Turbosqueeze: Fast, efficient, and eco-friendly compression for the future.**
