/*
 * Turbosqueeze optimal encoder.
 *
 * Copyright (c) 2024-2026 Julien Perrier-cornet
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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <algorithm>
#include <cctype>
#include <vector>

#include "turbosqueeze.h"
#include "platform.h"
#include "tsq_common.h"


static constexpr uint32_t TSQ_OPT_SORT_WINDOW_BYTES = 4 + 7 + 255;
static constexpr uint32_t TSQ_OPT_SORT_LAST_OFFSET = TSQ_OPT_SORT_WINDOW_BYTES - 8;


static void tsqLinearSearchDiff( uint32_t *ind, uint8_t *input, uint32_t start, uint32_t end, uint32_t offset, uint32_t size );


static inline uint64_t tsqLoadLinearWord( uint8_t *input, uint32_t rotation, uint32_t offset )
{
    return stdc_byteswap64( *((uint64_t*) &input[rotation + offset]) );
}


static int tsqCompareRotationsFull( uint8_t *input, uint32_t a, uint32_t b, uint32_t offset, uint32_t size )
{
    if (a == b)
        return 0;

    int cmp = memcmp(input + a + offset, input + b + offset, TSQ_OPT_SORT_WINDOW_BYTES - offset);
    if (cmp != 0)
        return cmp;

    return a < b ? -1 : 1;
}


static void tsqDebugPrintSortedRotations( uint32_t *ind, uint8_t *input, uint32_t size )
{
    if (size == 0 || size >= 1000)
    {
        return;
    }

    const uint32_t chars_to_print = std::min<uint32_t>(size, 80);

    printf("Sorted rotations (%u entries, %u chars shown):\n", size, chars_to_print);

    for (uint32_t row = 0; row < size; row++)
    {
        const uint32_t rotation = ind[row];
        printf("[%4u @ %4u] ", row, rotation);

        for (uint32_t column = 0; column < chars_to_print; column++)
        {
            const uint8_t value = input[(rotation + column) % size];
            putchar(std::isprint(value) ? value : '.');
        }

        putchar('\n');
    }
}


static void tsqSortQsort( uint32_t *ind, uint8_t *input, uint32_t start, uint32_t end, uint32_t offset, uint32_t size )
{
    // Use qsort to sort the indices
    std::sort( ind + start, ind + end, [&input, offset](uint32_t a, uint32_t b) {
        return tsqLoadLinearWord(input, a, offset) < tsqLoadLinearWord(input, b, offset);
    });

    tsqLinearSearchDiff( ind, input, start, end, offset, size );
}


static void tsqSortQsortFull( uint32_t *ind, uint8_t *input, uint32_t start, uint32_t end, uint32_t offset, uint32_t size )
{
    // Use qsort to sort the indices
    std::sort( ind + start, ind + end, [&input, offset, size](uint32_t a, uint32_t b)
    {
        return tsqCompareRotationsFull(input, a, b, offset, size) < 0;
    });
}


static void tsqLinearSearchDiff( uint32_t *ind, uint8_t *input, uint32_t start, uint32_t end, uint32_t offset, uint32_t size )
{
    uint64_t last = tsqLoadLinearWord(input, ind[start], offset);
    uint32_t is = start;
    uint32_t unsorted = 0;

    for (uint32_t i=start+1; i<end; i++)
    {
        uint64_t current = tsqLoadLinearWord(input, ind[i], offset);

        if (current != last)
        {
            // recursivity
            if ((i - is) > 1)
            {
                if (offset < 8)
                    tsqSortQsort( ind, input, is, i, offset+8, size );
                else
                    tsqSortQsortFull( ind, input, is, i, offset+8, size );

                unsorted += (i-is-1);
            }

            is = i;
            last = current;
        }
    }

    // Handle the last group
    if (is < end - 1)
    {
        if (offset < 8)
            tsqSortQsort( ind, input, is, end, offset+8, size );
        else if (offset < TSQ_OPT_SORT_LAST_OFFSET)
            tsqSortQsortFull( ind, input, is, end, offset+8, size );
    }
}


static uint32_t matchlen( uint8_t *input, uint32_t pos1, uint32_t pos2, uint32_t size )
{
    // Calculate the length of this LZ match (It's at least 4 bytes)
    // Bounds-safe: compare byte-by-byte within the valid buffer range
    uint32_t max_k = std::min( (uint32_t)(4+7+255), std::min( size - pos1, size - pos2 ) );
    max_k = std::min( max_k, pos2 - pos1 );
    uint32_t k = 0;

    // Fast 8-byte comparison while safe
    while (k + 8 <= max_k)
    {
        uint64_t xres = *((uint64_t*) &input[pos1 + k]) ^ *((uint64_t*) &input[pos2 + k]);
        if (xres != 0)
        {
            k += stdc_trailing_zeros_ull( xres ) >> 3;
            return k;
        }
        k += 8;
    }

    // Byte-by-byte for the remaining bytes
    while (k < max_k && input[pos1 + k] == input[pos2 + k])
        k++;

    return k;
}


/*
** We're taking advantage of the data structure we've built previously because the best LZ matches
** are located immediately around the current sorted index of all the possible rotations of the input.
** We just need to check if the offset is within usable range and voila.
*/
static bool searchBestLZMatch( uint8_t *input, uint32_t size, uint32_t pos, uint32_t *reverse_sorthits, uint32_t *sorthits, uint32_t chunk_base, uint32_t chunk_entries, uint32_t &maxk, uint32_t &maxpos )
{
    if (pos >= size || pos < chunk_base || (pos - chunk_base) >= chunk_entries)
    {
        maxk = 0;
        maxpos = 0xFFFFFFFF;
        return false;
    }

    const uint32_t pos_index = reverse_sorthits[pos - chunk_base];
    int64_t i = int64_t(pos_index) - 1;
    uint32_t j = pos_index + 1;
    bool updated;
    bool match_found = false, valid_hits_left = true, valid_hits_right = true;

    maxk = 0;
    maxpos = 0xFFFFFFFF;

    do
    {
        updated = false;

        // Search forward by one iteration in the sorted rotations as long as we get a better match than the current one
        if (j < chunk_entries)
        {
            const uint32_t candidate = sorthits[j];
            uint32_t k = matchlen(input, candidate, pos, size);

            if (candidate < pos && (pos - candidate) <= 0xFFFF && k >= 4)
            {
                if (k > maxk)
                {
                    maxk = k;
                    maxpos = candidate;
                    match_found = true;
                    updated = true;
                }
                else if (k == maxk && maxpos < candidate) // This is used to minimize the offset of the best LZ match.
                {
                    maxpos = candidate;
                    match_found = true;
                    updated = true;
                }
            }

            valid_hits_left &= k >= 4;
        }

        valid_hits_left &= j < chunk_entries;

        // Search backward by one iteration in the sorted rotations as long as we get a better match than the current one
        if (i >= 0)
        {
            const uint32_t candidate = sorthits[i];
            uint32_t k = matchlen(input, candidate, pos, size);

            if (candidate < pos && (pos - candidate) <= 0xFFFF && k >= 4)
            {
                if (k > maxk)
                {
                    maxk = k;
                    maxpos = candidate;
                    match_found = true;
                    updated = true;
                }
                else if (k == maxk && maxpos < candidate)
                {
                    maxpos = candidate;
                    match_found = true;
                    updated = true;
                }
            }

            valid_hits_right &= k >= 4;
        }

        valid_hits_right &= i >= 0;

        i--;
        j++;
    }
    while ((!match_found & (valid_hits_left | valid_hits_right)) || (match_found & updated));

    return match_found;
}


static void tsqBackwardsLZGreedy( TSQOptContext* ctx, uint8_t *input, uint32_t size )
{
    bool topliterals = false;
    uint32_t pos = size - 1;

    while (pos < size) // We stop when pos underflows, since it is an unsigned integer
    {
        bool hitfound = false;
        uint32_t i = pos;
        uint32_t j = 0;
        if (pos > (1 << 16)) j = (pos >> 16) - 1;
        uint32_t chunk_base = j * (1 << 16);
        uint32_t chunk_entries = std::min( (uint32_t)((j+2) * (1 << 16)), size ) - chunk_base;
        uint32_t maxk, maxpos, bestk, bestpos;

        while (i >= 3 && 
            searchBestLZMatch(input, size, i-3, ctx->reverse_sorthits[j], ctx->sorthits[j], chunk_base, chunk_entries, maxk, maxpos) && 
            maxk >= (pos - i + 4))
        {
            i--;
            bestk = maxk;
            bestpos = maxpos;
            hitfound = true;
        }

        if (hitfound)
        {
            //if (topliterals) printf( "lit %u pos %u\n", ctx->path.top().len, ctx->path.top().pos );

            TSQOptContext::TSQData data;

            data.hitpos = bestpos;
            data.len = pos - i + 3;
            pos -= data.len;
            data.pos = pos+1;
            data.type = 1; // LZ match

            assert( (data.pos - data.hitpos) <= 0xFFFF && data.hitpos + data.len <= data.pos && data.len >= 4 );

            //printf( "rep %u pos %u\n", data.len, data.pos );

            ctx->path.push(data);
            topliterals = false;
        }
        else
        {
            if (topliterals)
            {
                assert(!ctx->path.empty() && ctx->path.top().type == 2);
                ctx->path.top().pos--;
                ctx->path.top().len++;
            }
            else
            {
                TSQOptContext::TSQData data;

                data.pos = pos;
                data.hitpos = 0;
                data.len = 1;
                data.type = 2; // Literals

                ctx->path.push(data);
                topliterals = true;
            }

            pos--;
        }
    }

    //if (topliterals) printf( "lit %u pos %u\n", ctx->path.top().len, ctx->path.top().pos );
}


static inline void outputLits( uint8_t* input, uint8_t* output, uint32_t &i, uint32_t &last_i, uint32_t &j, uint32_t &last_size, uint32_t &n_sym )
{
    do
    {
        uint32_t incr = i-last_i > (8+255) ? (8+255) : i-last_i;

        uint8_t low = incr >= 8 ? 15 : incr + 7;
        uint8_t high = incr - 8;

        if (incr >= 8) output[j++] = high;

        tsq_memcpy8(&output[j], &input[last_i], incr);

        last_i += incr;
        j += incr;
        n_sym ++;

        output[last_size] = (output[last_size] << 4) | low; if ((n_sym & 1) == 0) { last_size = j++; }
    }
    while ((i-last_i) > 0) ;
}


void tsqEncode2_opt( TSQOptContext* ctx, uint8_t *input, uint8_t *output, uint32_t *outputSize, uint32_t inputSize )
{
    const uint32_t size = inputSize;

    // First write the uncompressed size
    output[0] = (size & 0xFF);
    output[1] = ((size >> 8) & 0xFF);
    output[2] = ((size >> 16) & 0xFF);

    std::vector<uint8_t> linear_input(size + TSQ_OPT_SORT_WINDOW_BYTES);

    if (size != 0)
    {
        memcpy(linear_input.data(), input, size);

        for (uint32_t i = 0; i < TSQ_OPT_SORT_WINDOW_BYTES; i++)
        {
            linear_input[size + i] = input[i % size];
        }
    }

    // Initialize sorthits with all rotation indices [0..size-1]
    const uint32_t n_blocks = (TSQ_BLOCK_SZ >> 16);

    for (uint32_t j=0; j<n_blocks-1; j++)
    {
        if ((j * (1 << 16)) > size) break;

        const uint32_t upper = std::min( (j+2) * (1 << 16), size ) - (j * (1 << 16));

        for (uint32_t i = 0; i < upper; i++)
        {
            ctx->sorthits[j][i] = j * (1 << 16) + i;
        }

        // Sort all rotations using the recursive quicksort approach
        tsqSortQsort( ctx->sorthits[j], linear_input.data(), 0, upper, 0, size );

        // reverse hits: map local position -> sort rank
        for (uint32_t i = 0; i < upper; i++)
        {
            ctx->reverse_sorthits[j][ctx->sorthits[j][i] - (j * (1 << 16))] = i;
        }
    }

    // Sort all rotations using the recursive quicksort approach
    //tsqSortQsort( ctx->sorthits, linear_input.data(), 0, size, 0, size );

    // Debug only on small buffers: less than 1000 bytes. Print out character by character, for 80 characters in the console, all the possible rotations in order in sortedhits.
    //tsqDebugPrintSortedRotations( ctx->sorthits, input, size );

    tsqBackwardsLZGreedy( ctx, input, size );

    uint32_t n_sym = 0;
    uint32_t j = 6;
    uint32_t last_size = j++;

    // go through the stack and encode each element found on it
    while (!ctx->path.empty())
    {
        TSQOptContext::TSQData data = ctx->path.top();

        if (data.type == 1)
        {
            uint32_t offset = data.pos - data.hitpos;
            uint32_t k = data.len;

            uint8_t low = k > (4+7) ? 7 : (k - 4);
            uint8_t high = k - 11;

            if (k >= (4+7)) output[j++] = high;

            output[j++] = offset & 0xFF;
            output[j++] = offset >> 8;

            // Complete/flush out control and size bytes?
            n_sym++;
            output[last_size] = (output[last_size] << 4) | low; if ((n_sym & 1) == 0) { last_size = j++; }

        }
        else if (data.type == 2)
        {
            uint32_t i = data.pos + data.len;
            outputLits( input, output, i, data.pos, j, last_size, n_sym );
        }

        ctx->path.pop();
    }

    output[3] = (n_sym & 0xFF);
    output[4] = ((n_sym >> 8) & 0xFF);
    output[5] = ((n_sym >> 16) & 0xFF);

    // Fill in remaining size byte if odd count
    if ((n_sym & 1) != 0)
    {
        output[last_size] <<= 4;
    }

    if (outputSize) *outputSize = j;
}
