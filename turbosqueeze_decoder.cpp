/*
Libturbosqueeze TurboSqueeze decoder.

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
#include <cassert>


#include "turbosqueeze_context.h"


// In the (near) future this function will be deprecated in favor of always using decoding with dictionnaries
// Consider this as a prototype/POC
extern "C" void turbosqueezeDecode( uint8_t *inputBlock, uint8_t *outputBlock, uint32_t *outputSize, uint32_t inputSize )
{
    uint32_t size = 0;
    static uint32_t block; block++;

    size = inputBlock[0];
    size |= inputBlock[1] << 8;
    size |= inputBlock[2] << 16;

    *outputSize = 0;

    // Corrupt data?
    if (size > TURBOSQUEEZE_BLOCK_SZ) return;

    uint32_t i=3, j=0;

    while (j < size)
    {
        uint8_t ctrl_byte = inputBlock[i]; i++;
        uint32_t ctrl_mask = 1 << 7;

        while (ctrl_mask)
        {
            uint32_t base = j;

            uint8_t ctr = inputBlock[i]; i++;

            uint32_t sz1 = (ctr >> 4) + 1;
            uint32_t offset1 = *((uint16_t*) (&inputBlock[i]));

            bool rep1 = (ctrl_byte & ctrl_mask) != 0;

            uint8_t *src1 = rep1 ? &outputBlock[base-offset1] : &inputBlock[i];

#ifdef TURBOSQUEEZE_DEBUG
            for (uint32_t k=0; (j+k)<size && k<sz1; k++)
            {
                assert( src1[k] == outputBlock[j+k] );
            }

            memcpy( &outputBlock[j], src1, sz1 );
#else
            turbosqueeze_memcpy8( &outputBlock[j], src1 );
            turbosqueeze_memcpy8( &outputBlock[j+8], &src1[8] );
#endif

            i += rep1 ? 2 : sz1;
            j += sz1;

            ctrl_mask >>= 1;

            bool rep2 = (ctrl_byte & ctrl_mask) != 0;

            uint32_t sz2 = (ctr & 0xF) + 1;
            uint32_t offset2 = *((uint16_t*) (&inputBlock[i]));

            uint8_t *src2 = rep2 ? &outputBlock[base-offset2] : &inputBlock[i];

#ifdef TURBOSQUEEZE_DEBUG
            for (uint32_t k=0; (j+k)<size && k<sz2; k++)
            {
                assert( src2[k] == outputBlock[j+k] );
            }

            memcpy( &outputBlock[j], src2, sz2 );
#else
            turbosqueeze_memcpy8( &outputBlock[j], src2 );
            turbosqueeze_memcpy8( &outputBlock[j+8], &src2[8] );
#endif

            i += rep2 ? 2 : sz2;
            j += sz2;

            ctrl_mask >>= 1;
        }
    }

    *outputSize = size;
}


extern "C" void turbosqueezeDecodeWithDictionnary( uint8_t *inputBlock, uint8_t *outputBlock, uint32_t *outputSize, uint32_t inputSize, uint32_t outputOffset )
{
}

