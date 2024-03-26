/*
Libturbosqueeze API.

BSD 3-Clause License

Copyright (c) 2024, Julien Perrier-cornet

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <time.h>


#include "turbosqueeze_context.h"


#ifdef AVX2
extern "C" void turbosqueezeDecodeInternalAVX2( uint8_t *memory, uint32_t inputStart[8], uint32_t inputSize[8], uint32_t outputStart[8], uint32_t outputSize[8] );
#endif

extern "C" void turbosqueeze_decompress( const char* inname, const char* outname )
{
    clock_t start = clock();

    FILE *in = fopen(inname, "rb");
    if (!in) return;

    FILE *out = fopen(outname, "wb");
    if (!out) return;

    uint8_t* buffer = (uint8_t*) align_alloc( MAX_CACHE_LINE_SIZE, (TURBOSQUEEZE_OUTPUT_SZ+TURBOSQUEEZE_BLOCK_SZ)*8*sizeof(uint8_t) );

    clock_t accumRead = 0, accumDecode = 0, accumWrite = 0;

    if (buffer)
    {
        do
        {
            uint32_t inputPos = 0;
            uint32_t outputPos = TURBOSQUEEZE_OUTPUT_SZ*8;

            uint32_t inputStart[8];
            uint32_t inputSize[8];
            uint32_t outputStart[8];
            uint32_t outputSize[8];

            clock_t startread = clock();

            uint32_t i = 0;

            for (; i<8; i++)
            {
                uint32_t to_read = fgetc(in);
                to_read += fgetc(in) << 8;
                to_read += fgetc(in) << 16;

                inputStart[i] = inputPos+3;
                inputSize[i] = to_read-3;
                outputStart[i] = outputPos;

                if (to_read > 0 && to_read < TURBOSQUEEZE_OUTPUT_SZ && !feof(in) && (to_read == fread( &buffer[inputPos], 1, to_read, in )))
                {
                    outputSize[i] = buffer[inputPos];
                    outputSize[i] |= buffer[inputPos+1] << 8;
                    outputSize[i] |= buffer[inputPos+2] << 16;
                    outputPos += outputSize[i];
                    inputPos += to_read;
                }
                else
                    break;
            }

            uint32_t last_i = i;

            for (; i<8; i++)
            {
                inputStart[i] = inputStart[last_i];
                inputSize[i] = inputSize[last_i];
                outputStart[i] = TURBOSQUEEZE_OUTPUT_SZ*8 + TURBOSQUEEZE_BLOCK_SZ*i;
                outputSize[i] = TURBOSQUEEZE_BLOCK_SZ;
            }

            clock_t startdecode = clock();

#ifdef AVX2
            turbosqueezeDecodeInternalAVX2( buffer, inputStart, inputSize, outputStart, outputSize );
#else
            //turbosqueezeDecode( inbuff, outbuff, &outputSize, to_read );
#endif

            clock_t startwrite = clock();

            for (i=0; i<last_i; i++)
            {
                fwrite( buffer+outputStart[i], 1, outputSize[i], out );
            }

            clock_t endwrite = clock();

            accumRead += startdecode - startread;
            accumDecode += startwrite - startdecode;
            accumWrite += endwrite - startwrite;
        }
        while ( !feof(in) ) ;
    }

    if (buffer != nullptr) align_free(buffer);

    printf("%s (%ld) -> %s (%ld) in %.3fs (Read %.3fs %.1fMB/s) (Write %.3fs %.1fMB/s) (Decode %.3fs %.1fMB/s)\n",
        inname, ftell(in), outname, ftell(out), double(clock()-start) / CLOCKS_PER_SEC,
        double(accumRead) / CLOCKS_PER_SEC, ftell(in) * 0.000001 / (double(accumRead) / CLOCKS_PER_SEC),
        double(accumWrite) / CLOCKS_PER_SEC, ftell(out) * 0.000001 / (double(accumWrite) / CLOCKS_PER_SEC),
        double(accumDecode) / CLOCKS_PER_SEC, ftell(out) * 0.000001 / (double(accumDecode) / CLOCKS_PER_SEC));

    fflush(out);
}
