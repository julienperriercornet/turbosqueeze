/*
Libturbosqueeze TurboSqueeze context alloc/dealloc utilities.

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


#include "turbosqueeze_context.h"


extern "C" void turbosqueezeDeallocateCompression(struct TSCompressionContext* ctx)
{
    if (ctx->refhash != nullptr) align_free(ctx->refhash);
    if (ctx->refhashcount != nullptr) align_free(ctx->refhashcount);
    align_free( ctx );
}


extern "C" struct TSCompressionContext* turbosqueezeAllocateCompression()
{
    struct TSCompressionContext* context = (struct TSCompressionContext*) align_alloc( MAX_CACHE_LINE_SIZE, sizeof(struct TSCompressionContext) );

    if (context)
    {
        context->refhash = nullptr;
        context->refhashcount = nullptr;

        context->refhash = (struct TSCompressionContext::SymRef*) align_alloc( MAX_CACHE_LINE_SIZE, TURBOSQUEEZE_REFHASH_SZ*TURBOSQUEEZE_REFHASH_ENTITIES*sizeof(struct TSCompressionContext::SymRef) );
        context->refhashcount = (uint8_t*) align_alloc( MAX_CACHE_LINE_SIZE, TURBOSQUEEZE_REFHASH_SZ*sizeof(uint8_t) );

        if (context->refhash == nullptr || context->refhashcount == nullptr)
        {
            turbosqueezeDeallocateCompression( context );
            context = nullptr;
        }
    }

    return context;
}

