/*
** Lib4zip lzaahe decoder implementation.
** Copyright (C) 2022-2024 Julien Perrier-cornet
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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#ifdef AVX2
#if _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
#endif

#include "tsq_decode.h"
#include "tsq_common.h"


void tsqDecodeNoext( uint8_t *inputBlock, uint8_t *outputBlock, uint32_t *outputSize, uint32_t inputSize )
{
    uint32_t size = 0;

    *outputSize = 0;

    // First we read the uncompressed size
    size = inputBlock[0];
    size |= inputBlock[1] << 8;
    size |= inputBlock[2] << 16;

    if (!( size <= TSQ_BLOCK_SZ )) return;

    uint32_t fastsize = size > 512 ? size - 256 : 0;

    uint32_t i = 3, j = 0;

    // Fast decompression loop that overruns data in the output buffer. We stop close to the end of the data to process more safely.
    while (j < fastsize)
    {
        uint8_t control_byte = inputBlock[i++];
        uint8_t control_mask = 128;

        #pragma unroll 4
        for (uint32_t k=0; k<4; k++)
        {
            uint8_t size_byte = inputBlock[i++];
            uint32_t rep_last_j = j;
            uint32_t sz = (size_byte>>4) + 1;

            uint32_t control = control_byte & control_mask;
            int32_t pos = rep_last_j - tsq_read16( &inputBlock[i] );

            tsq_memcpy16(&outputBlock[j], control ? &inputBlock[i] : &outputBlock[pos]);
            j += sz;
            i += control ? sz : 2;

            control_mask >>= 1;
            sz = (size_byte & 15) + 1;
            control = control_byte & control_mask;
            pos = rep_last_j - tsq_read16( &inputBlock[i] );

            tsq_memcpy16(&outputBlock[j], control ? &inputBlock[i] : &outputBlock[pos]);
            j += sz;
            i += control ? sz : 2;

            control_mask >>= 1;
        }
    }

    // Slow decompression using memcpy but yeilds the correct result without writing over outside of the output buffer/decompressed size.
    while (j < size)
    {
        uint8_t control_byte = inputBlock[i]; i++;
        uint8_t control_mask = 128;

        #pragma unroll 4
        for (uint32_t k=0; k<4; k++)
        {
            uint8_t size_byte = inputBlock[i++];
            uint32_t rep_last_j = j;
            uint32_t sz = (size_byte>>4) + 1;

            uint32_t control = control_byte & control_mask;
            int32_t pos = rep_last_j - tsq_read16( &inputBlock[i] );

            memcpy( &outputBlock[j], control ? &inputBlock[i] : &outputBlock[pos], sz );
            j += sz;
            i += control ? sz : 2;

            control_mask >>= 1;
            sz = (size_byte & 15) + 1;
            control = control_byte & control_mask;
            pos = rep_last_j - tsq_read16( &inputBlock[i] );

            memcpy( &outputBlock[j], control ? &inputBlock[i] : &outputBlock[pos], sz );
            j += sz;
            i += control ? sz : 2;

            control_mask >>= 1;
        }
    }

    *outputSize = size;
}


extern "C" void tsqDecode( uint8_t *inputBlock, uint8_t *outputBlock, uint32_t *outputSize, uint32_t inputSize, uint32_t withExtensions )
{
    if (!withExtensions)
    {
        tsqDecodeNoext( inputBlock, outputBlock, outputSize, inputSize );
        return;
    }

    uint32_t size = 0;

    *outputSize = 0;

    // First we read the uncompressed size
    size = inputBlock[0];
    size |= inputBlock[1] << 8;
    size |= inputBlock[2] << 16;

    if (!( size <= TSQ_BLOCK_SZ )) return;

    uint32_t fastsize = size > 512 ? size - 256 : 0;

    uint32_t i = 3, j = 0;

    // Fast decompression loop that overruns data in the output buffer. We stop close to the end of the data to process more safely.
    while (j < fastsize)
    {
        uint8_t control_byte = inputBlock[i++];
        uint8_t control_mask = 128;

        #pragma unroll 4
        for (uint32_t k=0; k<4; k++)
        {
            uint8_t size_byte = inputBlock[i++];
            uint32_t rep_last_j = j;
            uint32_t sz = (size_byte>>4) + 1;

            if (control_byte & control_mask)
            {
                tsq_memcpy16(&outputBlock[j], &inputBlock[i]);
                j += sz;
                i += sz;
            }
            else
            {
                int32_t pos = rep_last_j - tsq_read16( &inputBlock[i] );
                switch (sz)
                {
                case 1:
                    tsq_memcpy32(&outputBlock[j], &outputBlock[pos]);
                    j+=32;
                    break;
                case 2:
                    tsq_memcpy48((uint64_t*) &outputBlock[j], (uint64_t*) &outputBlock[pos]);
                    j+=48;
                    break;
                case 3:
                    tsq_memcpy64((uint64_t*) &outputBlock[j], (uint64_t*) &outputBlock[pos]);
                    j+=64;
                    break;
                default:
                    tsq_memcpy16(&outputBlock[j], &outputBlock[pos]);
                    j += sz;
                }
                i+=2;
            }

            control_mask >>= 1;
            sz = (size_byte & 15) + 1;

            if (control_byte & control_mask)
            {
                tsq_memcpy16(&outputBlock[j], &inputBlock[i]);
                j += sz;
                i += sz;
            }
            else
            {
                int32_t pos = rep_last_j - tsq_read16( &inputBlock[i] );
                switch (sz)
                {
                case 1:
                    tsq_memcpy32(&outputBlock[j], &outputBlock[pos]);
                    j+=32;
                    break;
                case 2:
                    tsq_memcpy48((uint64_t*) &outputBlock[j], (uint64_t*) &outputBlock[pos]);
                    j+=48;
                    break;
                case 3:
                    tsq_memcpy64((uint64_t*) &outputBlock[j], (uint64_t*) &outputBlock[pos]);
                    j+=64;
                    break;
                default:
                    tsq_memcpy16(&outputBlock[j], &outputBlock[pos]);
                    j += sz;
                }
                i+=2;
            }

            control_mask >>= 1;
        }
    }

    // Slow decompression using memcpy but yeilds the correct result without writing over outside of the output buffer/decompressed size.
    while (j < size)
    {
        uint8_t control_byte = inputBlock[i]; i++;
        uint8_t control_mask = 128;

        #pragma unroll 4
        for (uint32_t k=0; k<4; k++)
        {
            uint8_t size_byte = inputBlock[i]; i++;
            uint32_t rep_last_j = j;
            uint32_t sz = (size_byte>>4) + 1;

            if (control_byte & control_mask)
            {
                memcpy( &outputBlock[j], &inputBlock[i], sz );
                j += sz;
                i += sz;
            }
            else
            {
                uint32_t pos = rep_last_j - tsq_read16( &inputBlock[i] );
                switch (sz)
                {
                case 1:
                    memcpy( &outputBlock[j], &outputBlock[pos], 32 );
                    j+=32;
                    break;
                case 2:
                    memcpy( &outputBlock[j], &outputBlock[pos], 48 );
                    j+=48;
                    break;
                case 3:
                    memcpy( &outputBlock[j], &outputBlock[pos], 64 );
                    j+=64;
                    break;
                default:
                    memcpy( &outputBlock[j], &outputBlock[pos], sz );
                    j += sz;
                }
                i+=2;
            }

            if (!(j < size)) break;

            control_mask >>= 1;
            sz = (size_byte & 15) + 1;

            if (control_byte & control_mask)
            {
                memcpy( &outputBlock[j], &inputBlock[i], sz );
                j += sz;
                i += sz;
            }
            else
            {
                uint32_t pos = rep_last_j - tsq_read16( &inputBlock[i] );
                switch (sz)
                {
                case 1:
                    memcpy( &outputBlock[j], &outputBlock[pos], 32 );
                    j+=32;
                    break;
                case 2:
                    memcpy( &outputBlock[j], &outputBlock[pos], 48 );
                    j+=48;
                    break;
                case 3:
                    memcpy( &outputBlock[j], &outputBlock[pos], 64 );
                    j+=64;
                    break;
                default:
                    memcpy( &outputBlock[j], &outputBlock[pos], sz );
                    j += sz;
                }
                i+=2;
            }

            control_mask >>= 1;
        }
    }

    *outputSize = size;
}

