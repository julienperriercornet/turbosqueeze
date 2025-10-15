#pragma once

/*
** Lib4zip line of sight (los) codec common include file.
** Copyright (C) 2024 Julien Perrier-cornet
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program.  If not, see <http://www.gnu.org/licenses/>.
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

