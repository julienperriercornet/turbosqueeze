#pragma once

/*
 * Turbosqueeze common helper functions between the encoder and the decoder.
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

#include <cassert>
#include <cstring>


static void tsq_init( struct TSQCompressionContext* ctx )
{
    memset( ctx->refhash, 0, TSQ_HASH_SZ );
}

static inline void tsq_memcpy16( void* dst, void* src )
{
#ifdef AVX2
    _mm_storeu_si128((__m128i*) dst, _mm_lddqu_si128((__m128i const*) src));
#else
    uint64_t tmp0 = *((uint64_t*) src);
    uint64_t tmp1 = *((uint64_t*) src+1);
    *((uint64_t*) dst) = tmp0;
    *((uint64_t*) dst+1) = tmp1;
#endif
}


static inline void tsq_memcpy16_compat( void* dst, void* src )
{
    uint64_t tmp0 = *((uint64_t*) src);
    uint64_t tmp1 = *((uint64_t*) src+1);
    *((uint64_t*) dst) = tmp0;
    *((uint64_t*) dst+1) = tmp1;
}


static inline void tsq_memcpy32( void* dst, void* src )
{
#ifdef AVX2
    _mm256_storeu_si256((__m256i*) dst, _mm256_loadu_si256((__m256i const*) src));
#else
    uint64_t tmp0 = *((uint64_t*) src);
    uint64_t tmp1 = *((uint64_t*) src+1);
    uint64_t tmp2 = *((uint64_t*) src+2);
    uint64_t tmp3 = *((uint64_t*) src+3);
    *((uint64_t*) dst) = tmp0;
    *((uint64_t*) dst+1) = tmp1;
    *((uint64_t*) dst+2) = tmp2;
    *((uint64_t*) dst+3) = tmp3;
#endif
}

static inline void tsq_memcpy48( uint64_t* dst, uint64_t* src )
{
#ifdef AVX2
    _mm256_storeu_si256((__m256i*) dst, _mm256_loadu_si256((__m256i const*) src));
    _mm_storeu_si128((__m128i*) (dst+4), _mm_lddqu_si128((__m128i const*) (src+4)));
#else
    uint64_t tmp0 = *((uint64_t*) src);
    uint64_t tmp1 = *((uint64_t*) src+1);
    uint64_t tmp2 = *((uint64_t*) src+2);
    uint64_t tmp3 = *((uint64_t*) src+3);
    uint64_t tmp4 = *((uint64_t*) src+4);
    uint64_t tmp5 = *((uint64_t*) src+5);
    *((uint64_t*) dst) = tmp0;
    *((uint64_t*) dst+1) = tmp1;
    *((uint64_t*) dst+2) = tmp2;
    *((uint64_t*) dst+3) = tmp3;
    *((uint64_t*) dst+4) = tmp4;
    *((uint64_t*) dst+5) = tmp5;
#endif
}

static inline void tsq_memcpy64( uint64_t* dst, uint64_t* src )
{
#ifdef AVX2
    _mm256_storeu_si256((__m256i*) dst, _mm256_loadu_si256((__m256i const*) src));
    _mm256_storeu_si256((__m256i*) (dst+4), _mm256_loadu_si256((__m256i const*) (src+4)));
#else
    uint64_t tmp0 = *((uint64_t*) src);
    uint64_t tmp1 = *((uint64_t*) src+1);
    uint64_t tmp2 = *((uint64_t*) src+2);
    uint64_t tmp3 = *((uint64_t*) src+3);
    uint64_t tmp4 = *((uint64_t*) src+4);
    uint64_t tmp5 = *((uint64_t*) src+5);
    uint64_t tmp6 = *((uint64_t*) src+6);
    uint64_t tmp7 = *((uint64_t*) src+7);
    *((uint64_t*) dst) = tmp0;
    *((uint64_t*) dst+1) = tmp1;
    *((uint64_t*) dst+2) = tmp2;
    *((uint64_t*) dst+3) = tmp3;
    *((uint64_t*) dst+4) = tmp4;
    *((uint64_t*) dst+5) = tmp5;
    *((uint64_t*) dst+6) = tmp6;
    *((uint64_t*) dst+7) = tmp7;
#endif
}

static uint32_t tsq_read16( void* ptr )
{
    return *((uint16_t*) ptr);
}

