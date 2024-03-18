Turbosqeeze

The turbosqueeze library is an optimized replacement for the lossless compression library lz4. 

Typical decompression speeds are twice higher than for the same file encoded by the lz4 library (memory to memory).

For the moment only single core compression and decompression is implemented but it's possible to add multithreaded compression and decompression.

The reason for choosing to make a lossless compression library is because of the climate impact of these software bricks. If we can acheive a twice higher performance on this common task, then energy consumption for acheiving this task is divided by 2 as a result. Should this library be adopted in as many places as the lz4 library, the climate impact would be quite significant, saving about 1 million tons of CO2 emissions per year thanks to less than 1k lines of C code. 

