/*
Libturbosqueeze TurboSqueeze avx2 decoder.

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

#if _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif


#include "turbosqueeze_context.h"


#ifdef __AVX2__

static __m256i constant_0 = _mm256_set1_epi32( 0 );
static __m256i constant_1 = _mm256_set1_epi32( 1 );
static __m256i constant_2 = _mm256_set1_epi32( 2 );
static __m256i constant_3 = _mm256_set1_epi32( 3 );
static __m256i constant_15 = _mm256_set1_epi32( 15 );
static __m256i constant_128 = _mm256_set1_epi32( 128 );
static __m256i constant_256 = _mm256_set1_epi32( 256 );


static inline __m256i _mm256_i32gather_u8_to_epi32( void* memory, __m256i indices )
{
    return _mm256_srlv_epi32( _mm256_i32gather_epi32( (int*) memory, _mm256_srli_epi32( indices, 2 ), 1 ), _mm256_slli_epi32( _mm256_and_si256( indices, constant_3 ), 3 ) );
}

static inline __m256i _mm256_select_epi32( __m256i a, __m256i b, __m256i mask )
{
    return _mm256_or_si256(_mm256_and_si256( a, mask ), _mm256_andnot_si256( b, mask ));
}

#endif


extern "C" void turbosqueezeDecodeInternalAVX2( uint8_t *memory, uint32_t inputStart[8], uint32_t inputSize[8], uint32_t outputStart[8], uint32_t outputSize[8] )
{
#ifdef __AVX2__

    /*
    uint32_t tmpsizes[8];
    uint32_t i_indices[8] = { (1 << 16), (6 << 16), (11 << 16), (16 << 16), (21 << 16), (26 << 16), (31 << 16), (36 << 16) };

    for (uint32_t i=0; i<8; i++)
    {
        tmpsizes[i] = memory[i_indices[i]];
        tmpsizes[i] |= memory[i_indices[i]+1] << 8;
        tmpsizes[i] |= memory[i_indices[i]+2] << 16;
    }

    uint32_t sizesum = (40 << 16); // Starting indice of the decompressed stream

    uint32_t j_indices[8];

    for (uint32_t i=0; i<8; i++)
    {
        j_indices[i] = sizesum;
        sizesum += tmpsizes[i];
    }

    // *outputSize = 0;

    // Corrupt data?
    if (tmpsize > TURBOSQUEEZE_BLOCK_SZ) return;
    */

    __m256i i = _mm256_loadu_si256((__m256i*) &inputStart[0]);
    __m256i j = _mm256_loadu_si256((__m256i*) &outputStart[0]);
    __m256i tmpsize = _mm256_loadu_si256((__m256i*) &outputSize[0]);
    __m256i sizem = _mm256_sub_epi32( _mm256_add_epi32( j, tmpsize ), constant_256 );
    // We stop at least one block before the end to decode the end safely

    while (_mm256_movemask_epi8(_mm256_cmpgt_epi32(sizem, j)) == 0xFFFFFFFF)
    {
        //uint8_t ctrl_byte = inputBlock[i]; i++;
        __m256i control_byte = _mm256_i32gather_u8_to_epi32( memory, i );
        i = _mm256_add_epi32( i, constant_1 );

        //uint32_t ctrl_mask = 1 << 7;
        __m256i control_mask = constant_128;

        #pragma unroll 4
        for (uint32_t k=0; k<4; k++)
        {
            //uint32_t base = j;
            __m256i base = j;

            //uint8_t ctr = inputBlock[i]; i++;
            __m256i counter = _mm256_i32gather_u8_to_epi32( memory, i );
            i = _mm256_add_epi32( i, constant_1 );

            //uint32_t sz1 = (ctr >> 4) + 1;
            __m256i sz1 = _mm256_add_epi32( _mm256_srli_epi32( counter, 4 ), constant_1 );
            //uint32_t offset1 = *((uint16_t*) (&inputBlock[i]));
            __m256i offset1_l = _mm256_i32gather_u8_to_epi32( memory, i );
            __m256i offset1_h = _mm256_i32gather_u8_to_epi32( memory, _mm256_add_epi32( i, constant_1 ) );
            __m256i offset1 = _mm256_or_si256( offset1_l, _mm256_slli_epi32( offset1_h, 8 ));

            // bool rep1 = (ctrl_byte & ctrl_mask) == 0;
            __m256i rep1 = _mm256_cmpeq_epi32( _mm256_and_si256( control_byte, control_mask ), constant_0 );

            //uint8_t *src1 = rep1 ? &inputBlock[i] : &outputBlock[base-offset1];
            union src {
                __m256i src1;
                uint32_t src8[8];
            } src;
            _mm256_storeu_si256( (__m256i*) &src.src1, _mm256_select_epi32( i, _mm256_sub_epi32( base, offset1 ), rep1 ) );

            union outputBlock {
                __m256i j;
                uint32_t j8[8];
            } outputBlock;
            _mm256_storeu_si256( (__m256i*) &outputBlock.j, j );

            //_mm_storeu_si128( (__m128i_u*) (memory+outputBlock.j8[0]), _mm_lddqu_si128( (__m128i_u*) (memory+src.src8[0]) ));
            //_mm_storeu_si128( (__m128i_u*) (memory+outputBlock.j8[1]), _mm_lddqu_si128( (__m128i_u*) (memory+src.src8[1]) ));
            //_mm_storeu_si128( (__m128i_u*) (memory+outputBlock.j8[2]), _mm_lddqu_si128( (__m128i_u*) (memory+src.src8[2]) ));
            //_mm_storeu_si128( (__m128i_u*) (memory+outputBlock.j8[3]), _mm_lddqu_si128( (__m128i_u*) (memory+src.src8[3]) ));
            //_mm_storeu_si128( (__m128i_u*) (memory+outputBlock.j8[4]), _mm_lddqu_si128( (__m128i_u*) (memory+src.src8[4]) ));
            //_mm_storeu_si128( (__m128i_u*) (memory+outputBlock.j8[5]), _mm_lddqu_si128( (__m128i_u*) (memory+src.src8[5]) ));
            //_mm_storeu_si128( (__m128i_u*) (memory+outputBlock.j8[6]), _mm_lddqu_si128( (__m128i_u*) (memory+src.src8[6]) ));
            //_mm_storeu_si128( (__m128i_u*) (memory+outputBlock.j8[7]), _mm_lddqu_si128( (__m128i_u*) (memory+src.src8[7]) ));

            // i += rep1 ? sz1 : 2;
            i = _mm256_add_epi32( i, _mm256_select_epi32( sz1, constant_2, rep1 ) );
            // j += sz1;
            j = _mm256_add_epi32( j, sz1 );

            // ctrl_mask >>= 1;
            control_mask = _mm256_srli_epi32( control_mask, 1 );

            //bool rep2 = (ctrl_byte & ctrl_mask) == 0;
            __m256i rep2 = _mm256_cmpeq_epi32( _mm256_and_si256( control_byte, control_mask ), constant_0 );

            //uint32_t sz2 = (ctr & 0xF) + 1;
            __m256i sz2 = _mm256_add_epi32( _mm256_and_si256( counter, constant_15 ), constant_1 );
            //uint32_t offset2 = *((uint16_t*) (&inputBlock[i]));
            __m256i offset2_l = _mm256_i32gather_u8_to_epi32( memory, i );
            __m256i offset2_h = _mm256_i32gather_u8_to_epi32( memory, _mm256_add_epi32( i, constant_1 ) );
            __m256i offset2 = _mm256_or_si256( offset2_l, _mm256_slli_epi32( offset2_h, 8 ));

            src.src1 = _mm256_select_epi32( i, _mm256_sub_epi32( base, offset2 ), rep2 );
            outputBlock.j = j;

            //_mm_storeu_si128( (__m128i_u*) (memory+outputBlock.j8[0]), _mm_lddqu_si128( (__m128i_u*) (memory+src.src8[0]) ));
            //_mm_storeu_si128( (__m128i_u*) (memory+outputBlock.j8[1]), _mm_lddqu_si128( (__m128i_u*) (memory+src.src8[1]) ));
            //_mm_storeu_si128( (__m128i_u*) (memory+outputBlock.j8[2]), _mm_lddqu_si128( (__m128i_u*) (memory+src.src8[2]) ));
            //_mm_storeu_si128( (__m128i_u*) (memory+outputBlock.j8[3]), _mm_lddqu_si128( (__m128i_u*) (memory+src.src8[3]) ));
            //_mm_storeu_si128( (__m128i_u*) (memory+outputBlock.j8[4]), _mm_lddqu_si128( (__m128i_u*) (memory+src.src8[4]) ));
            //_mm_storeu_si128( (__m128i_u*) (memory+outputBlock.j8[5]), _mm_lddqu_si128( (__m128i_u*) (memory+src.src8[5]) ));
            //_mm_storeu_si128( (__m128i_u*) (memory+outputBlock.j8[6]), _mm_lddqu_si128( (__m128i_u*) (memory+src.src8[6]) ));
            //_mm_storeu_si128( (__m128i_u*) (memory+outputBlock.j8[7]), _mm_lddqu_si128( (__m128i_u*) (memory+src.src8[7]) ));

            //i += rep2 ? 2 : sz2;
            i = _mm256_add_epi32( i, _mm256_select_epi32( sz2, constant_2, rep2 ) );
            //j += sz2;
            j = _mm256_add_epi32( j, sz2 );

            //ctrl_mask >>= 1;
            control_mask = _mm256_srli_epi32( control_mask, 1 );
        }
    }

    //*outputSize = size;
#endif
}
