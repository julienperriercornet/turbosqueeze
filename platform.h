#pragma once

/*
** Turbosqueeze platform specific hacks.
** Copyright (C) 2024-2025 Nulang Solutions Developer team
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


#if _MSC_VER
#define align_alloc( A, B ) _aligned_malloc( B, A )
#define align_free( A ) _aligned_free( A )
#else
#define align_alloc( A, B ) aligned_alloc( A, B )
#define align_free( A ) free( A )
#endif

#if _MSC_VER
#include <immintrin.h>
static int stdc_trailing_zeros_ull( unsigned long long value )
{
    return (int) _tzcnt_u64( value );
}
#else
static int stdc_trailing_zeros_ull( unsigned long long value )
{
    return value ? __builtin_ctzll( value ) : 64;
}
#endif

#if _MSC_VER
#include <immintrin.h>
static unsigned long long stdc_byteswap64( unsigned long long value )
{
    return _byteswap_uint64( value );
}
#else
static unsigned long long stdc_byteswap64( unsigned long long value )
{
    return __builtin_bswap64( value );
}
#endif

#define MAX_CACHE_LINE_SIZE 128

#ifdef AVX2
#if _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
#endif
