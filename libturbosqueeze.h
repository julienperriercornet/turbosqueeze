#pragma once

/*

libturbosqueeze general API include file.

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


#include <cstdint>


// Enables a test mode to debug corrupt output
//#define TURBOSQUEEZE_DEBUG


#define TURBOSQUEEZE_BLOCK_BITS (18)
#define TURBOSQUEEZE_BLOCK_SZ (1<<TURBOSQUEEZE_BLOCK_BITS)
#define TURBOSQUEEZE_OUTPUT_SZ ((1<<TURBOSQUEEZE_BLOCK_BITS) + (1<<(TURBOSQUEEZE_BLOCK_BITS-2)))


#pragma pack(1)
struct TSCompressionContext {
    // LZ string matching
    struct SymRefFast {
        uint32_t sym4;
        uint32_t latest_pos;
    };
    struct SymRef {
        uint32_t sym4;
        uint32_t position;
        uint32_t n_occurences;
    };
    struct SymRefFast *refhash;
    struct SymRef *hash;
    uint32_t *positions;
    uint8_t *refhashcount;
    uint32_t posIdx;
    uint32_t compressionLevel;
};
#pragma pack()


#if defined (__cplusplus)
extern "C" {
#endif

    struct TSCompressionContext* turbosqueezeAllocateCompression( uint32_t n );
    void turbosqueezeDeallocateCompression(struct TSCompressionContext* ctx);
    void turbosqueezeEncode( struct TSCompressionContext* ctx, uint8_t *inputBlock, uint8_t *outputBlock, uint32_t *outputSize, uint32_t inputSize );
    void turbosqueezeEncodeWithDictionnary( struct TSCompressionContext* ctx, uint8_t *inputBlock, uint8_t *outputBlock, uint32_t *outputSize, uint32_t inputSize, uint32_t inputOffset );

    void turbosqueezeDecode( uint8_t *inputBlock, uint8_t *outputBlock, uint32_t *outputSize, uint32_t inputSize );
    void turbosqueezeDecodeWithDictionnary( uint8_t *inputBlock, uint8_t *outputBlock, uint32_t *outputSize, uint32_t inputSize, uint32_t outputOffset );

    void turbosqueezeGetDictionnary( uint16_t dictionnary, uint8_t *block, uint32_t maxdictsize, uint32_t *dictionnarySize );
    void turbosqueezeGetDictionnaryFromContext( struct TSCompressionContext* ctx, uint8_t *block, uint32_t maxdictsize, uint32_t *dictionnarySize );

#if defined (__cplusplus)
}
#endif


