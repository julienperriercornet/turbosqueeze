/*
Libturbosqueeze TurboSqueeze encoder.

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


static void init(struct TSCompressionContext* ctx)
{
    if (ctx->compressionLevel == 0)
    {
        memset( ctx->refhashcount, 0, TURBOSQUEEZE_REFHASH_SZ*sizeof(uint8_t) );
    }
    else
    {
        memset( ctx->refhashcount, 0, TURBOSQUEEZE_REFHASH_PLUS_SZ*sizeof(uint8_t) );
    }

    ctx->posIdx = 0;
}


static inline uint32_t getHash( uint32_t h )
{
    return (((h & (0xFFFFFFFF - (TURBOSQUEEZE_REFHASH_SZ - 1))) >> (32-TURBOSQUEEZE_REFHASH_BITS)) ^ (h & (TURBOSQUEEZE_REFHASH_SZ - 1)));
}


static inline uint32_t getHash2( uint32_t h )
{
    return (((h & (0xFFFFFFFF - (TURBOSQUEEZE_REFHASH_PLUS_SZ - 1))) >> (32-TURBOSQUEEZE_BLOCK_BITS)) ^ (h & (TURBOSQUEEZE_REFHASH_PLUS_SZ - 1)));
}


static inline uint32_t matchlen( uint8_t *inbuff, uint32_t first, uint32_t second, uint32_t decoded_size, uint32_t size )
{
    uint32_t maxmatchstrlen = 16;

    maxmatchstrlen = (first+maxmatchstrlen < decoded_size) ? maxmatchstrlen : decoded_size-first;
    maxmatchstrlen = (second+maxmatchstrlen) < size ? maxmatchstrlen : size - second;
    maxmatchstrlen = (second-first) < maxmatchstrlen ? second-first : maxmatchstrlen;

    if (maxmatchstrlen >= 4)
    {
        uint32_t i = 4;
        uint8_t *strfirst = inbuff+first;
        uint8_t *strsecond = inbuff+second;

        while ((i != maxmatchstrlen) && (strfirst[i] == strsecond[i])) i++;

        return i;
    }
    else
        return 0;
}


static inline bool addHitFast( struct TSCompressionContext *context, uint8_t *input, uint32_t i, uint32_t decoded_size, uint32_t size, uint32_t &hitlength, uint32_t &hitpos)
{
    if (i < size-3)
    {
        uint32_t str4 = *((uint32_t*) (input+i));
        uint32_t hash = getHash(str4);
        uint32_t hitidx = hash*TURBOSQUEEZE_REFHASH_ENTITIES;
        uint32_t j = 0;

        while (j < context->refhashcount[hash] && context->refhash[hitidx].sym4 != str4)
        {
            j++;
            hitidx++;
        }

        if (j < context->refhashcount[hash])
        {
            // Hit sym
            uint32_t matchlength = matchlen( input, context->refhash[hitidx].latest_pos, i, decoded_size, size );

            if (matchlength >= 4)
            {
                hitlength = matchlength;
                hitpos = context->refhash[hitidx].latest_pos;

                context->refhash[hitidx].latest_pos = i;

                return true;
            }
        }
        else if (j < TURBOSQUEEZE_REFHASH_ENTITIES)
        {
            // New sym
            context->refhash[hitidx].sym4 = str4;
            context->refhash[hitidx].latest_pos = i;

            context->refhashcount[hash]++;
        }
    }

    return false;
}


static inline bool addHit( struct TSCompressionContext *context, uint8_t *input, uint32_t i, uint32_t decoded_size, uint32_t size, uint32_t &hitlength, uint32_t &hitpos)
{
    if (i < size-3)
    {
        uint32_t str4 = *((uint32_t*) (input+i));
        uint32_t hash = getHash2(str4);
        uint32_t hitidx = hash*TURBOSQUEEZE_REFHASH_ENTITIES;
        uint32_t j = 0;

        while (j < context->refhashcount[hash] && context->hash[hitidx].sym4 != str4)
        {
            j++;
            hitidx++;
        }

        if (j < context->refhashcount[hash])
        {
            if (context->hash[hitidx].n_occurences == 1)
            {
                uint32_t matchlength = matchlen( input, context->hash[hitidx].position, i, decoded_size, size );

                if (matchlength >= 4)
                {
                    context->hash[hitidx].n_occurences++;

                    hitlength = matchlength;
                    hitpos = context->hash[hitidx].position;

                    // allocate hits
                    uint32_t firstpos = context->hash[hitidx].position;
                    uint32_t pos = context->hash[hitidx].position = context->posIdx;

                    context->positions[pos] = firstpos;
                    context->positions[pos+1] = i;

                    context->posIdx += context->compressionLevel*8;

                    return true;
                }
            }
            else
            {
                uint32_t n_occ = context->hash[hitidx].n_occurences > context->compressionLevel*8 ? context->compressionLevel*8 : context->hash[hitidx].n_occurences;
                uint32_t pos = context->hash[hitidx].position;
                uint32_t maxmatchlength = 0;
                uint32_t maxmatchpos = 0xFFFFFFFF;

                for (uint32_t k=0; k<n_occ; k++)
                {
                    if ((decoded_size - context->positions[pos+k]) < ((1<<16) - 32))
                    {
                        uint32_t matchlength = matchlen( input, context->positions[pos+k], i, decoded_size, size );

                        if (matchlength > maxmatchlength)
                        {
                            maxmatchlength = matchlength;
                            maxmatchpos = context->positions[pos+k];
                        }
                    }
                }

                if (maxmatchlength >= 4)
                {
                    context->positions[pos+(context->hash[hitidx].n_occurences%(context->compressionLevel*8))] = i;
                    context->hash[hitidx].n_occurences++;

                    hitlength = maxmatchlength;
                    hitpos = maxmatchpos;

                    return true;
                }
            }
        }
        else if (j < TURBOSQUEEZE_REFHASH_ENTITIES)
        {
            // New sym
            context->hash[hitidx].sym4 = str4;
            context->hash[hitidx].position = i;
            context->hash[hitidx].n_occurences = 1;

            context->refhashcount[hash]++;
        }
    }

    return false;
}


struct seqEntry {
    bool repeat;
    uint8_t size;
    uint32_t position;
    uint32_t base;
};


static uint32_t writeOutput( struct seqEntry *entryBuffer, uint32_t *entryPos, uint8_t *outptr, uint8_t *input, bool finalize, uint32_t processed )
{
    uint8_t ctrl_byte = entryBuffer[0].repeat;
    uint32_t i = 0;

    for (uint32_t j=1; j<8; j++)
    {
        ctrl_byte = (ctrl_byte << 1) | entryBuffer[j].repeat;
    }

    outptr[i++] = ctrl_byte;

    for (uint32_t j=0; j < 4 && ((j*2) < (*entryPos)); j++)
    {
        uint8_t size_byte = ((entryBuffer[j*2].size - 1) << 4) |  (entryBuffer[j*2+1].size - 1);

        outptr[i++] = size_byte;

        if (entryBuffer[j*2].repeat)
        {
            uint32_t offset = entryBuffer[j*2].base - entryBuffer[j*2].position;
            outptr[i] = offset & 0xFF;
            outptr[i+1] = (offset >> 8) & 0xFF;
            i += 2;
        }
        else
        {
            turbosqueeze_memcpy8( &outptr[i], &input[entryBuffer[j*2].position] );
            turbosqueeze_memcpy8( &outptr[i+8], &input[entryBuffer[j*2].position+8] );
            i += entryBuffer[j*2].size;
        }

        if (entryBuffer[j*2+1].repeat)
        {
            uint32_t offset = entryBuffer[j*2].base - entryBuffer[j*2+1].position;
            outptr[i] = offset & 0xFF;
            outptr[i+1] = (offset >> 8) & 0xFF;
            i += 2;
        }
        else
        {
            turbosqueeze_memcpy8( &outptr[i], &input[entryBuffer[j*2+1].position] );
            turbosqueeze_memcpy8( &outptr[i+8], &input[entryBuffer[j*2+1].position+8] );
            i += entryBuffer[j*2+1].size;
        }
    }

    if ((*entryPos) >= 8)
        *entryPos -= 8;
    else
        *entryPos = 0;

    if ((*entryPos) == 1)
    {
        entryBuffer[0].repeat = entryBuffer[8].repeat;
        entryBuffer[0].size = entryBuffer[8].size;
        entryBuffer[0].position = entryBuffer[8].position;
        entryBuffer[0].base = entryBuffer[8].base;

        if (finalize)
        {
            i += writeOutput( entryBuffer, entryPos, outptr+i, input, true, processed+i );
        }
    }

    return i;
}

extern "C" void turbosqueezeEncode( struct TSCompressionContext* ctx, uint8_t *inputBlock, uint8_t *outputBlock, uint32_t *outputSize, uint32_t inputSize )
{
    const uint32_t size = inputSize;
    static uint32_t block; block++;

    // First write the uncompressed size
    outputBlock[0] = (size & 0xFF);
    outputBlock[1] = ((size >> 8) & 0xFF);
    outputBlock[2] = ((size >> 16) & 0xFF);

    *outputSize = 3;

    init( ctx );

    uint32_t entryPos = 0;
    struct seqEntry entryBuffer[9];

    uint32_t i = 0;
    uint32_t j = 3;
    uint32_t last_i = i;
    uint32_t rep_last_i = i;
    uint8_t *outptr = outputBlock;

    while (i < size)
    {
        bool hit = false;
        uint32_t hitlength = -1;
        uint32_t hitpos = -1;

        last_i = i;

        // Count NoHit characters
        if (ctx->compressionLevel == 0)
        {
            while ((i < size) && ((i-last_i) < 16))
            {
                hit = addHitFast( ctx, inputBlock, i, rep_last_i, size, hitlength, hitpos );
                hit = hit && ((rep_last_i - hitpos) < ((1<<16) - 32)) && ((hitpos + hitlength) < rep_last_i);
                if (hit) break;
                i++;
            }
        }
        else
        {
            while ((i < size) && ((i-last_i) < 16))
            {
                hit = addHit( ctx, inputBlock, i, rep_last_i, size, hitlength, hitpos );
                hit = hit && ((rep_last_i - hitpos) < ((1<<16) - 32)) && ((hitpos + hitlength) < rep_last_i);
                if (hit) break;
                i++;
            }
        }

        // Litterals
        if ((i-last_i) > 0)
        {
            entryBuffer[entryPos].repeat = false;
            entryBuffer[entryPos].size = i-last_i;
            entryBuffer[entryPos].position = last_i;
            entryBuffer[entryPos].base = rep_last_i;
            entryPos++;

            if ((entryPos & 1) == 0)
                rep_last_i = i;
        }

        // Repeat
        if (hit)
        {
            entryBuffer[entryPos].repeat = true;
            entryBuffer[entryPos].size = hitlength;
            entryBuffer[entryPos].position = hitpos;
            entryBuffer[entryPos].base = rep_last_i;

            entryPos++;

            i += hitlength;

            if ((entryPos & 1) == 0)
                rep_last_i = i;
        }

        // Write output/flush?
        if (entryPos >= 8)
        {
            j += writeOutput( &entryBuffer[0], &entryPos, outptr+j, inputBlock, false, j );
        }
    }

    // Remaining litterals at the end of the buffer?
    if ((i-last_i) > 0)
    {
        entryBuffer[entryPos].repeat = false;
        entryBuffer[entryPos].size = i-last_i;
        entryBuffer[entryPos].position = last_i;
        entryBuffer[entryPos].base = rep_last_i;
        entryPos++;
    }

    // Finalize stream
    j += writeOutput( &entryBuffer[0], &entryPos, outptr+j, inputBlock, true, j );

    *outputSize = j;
}


extern "C" void turbosqueezeEncodeWithDictionnary( struct TSCompressionContext* ctx, uint8_t *inputBlock, uint8_t *outputBlock, uint32_t *outputSize, uint32_t inputSize, uint32_t inputOffset )
{
}

