
#include <cstdio>
#include <cstdlib>
#include <cstring>


#include "../tsq_context.h"


int test_tsq_context()
{
    TSQCompressionContext* context = tsqAllocateContext();

    if (context != nullptr)
    {
        tsqDeallocateContext(context);
        return 0;
    }
    else
    {
        return 1;
    }
}


const char *testinput = "The names \"John Doe\" for males, \"Jane Doe\" or \"Jane Roe\" for females, or \"Jonnie Doe\" and \"Janie Doe\" for children, or just \"Doe\" non-gender-specifically are used as placeholder names for a party whose true identity is unknown or must be withheld in a legal action, case, or discussion. The names are also used to refer to acorpse or hospital patient whose identity is unknown. This practice is widely used in the United States and Canada, but is rarely used in other English-speaking countries including the United Kingdom itself, from where the use of \"John Doe\" in a legal context originates. The names Joe Bloggs or John Smith are used in the UK instead, as well as in Australia and New Zealand.";



int test_tsq_compress()
{
    char compressed[700];
    char uncompressed[700];

    TSQCompressionContext* context = tsqAllocateContext();

    if (context != nullptr)
    {
        uint32_t compressedSz;
        uint32_t uncompressedSz;

        tsqEncode( context, (uint8_t *) testinput, (uint8_t *) &compressed[0], &compressedSz, 700, true );
        tsqDecode( (uint8_t *) &compressed[0], (uint8_t *) &uncompressed[0], &uncompressedSz, compressedSz, true );

        tsqDeallocateContext(context);

        return strncmp( testinput, &uncompressed[0], 700 );
    }
    else
    {
        return 1;
    }
}


int test_tsq_context_mt()
{
    TSQCompressionContext_MT* context = tsqAllocateContextCompression_MT( false );

    if (context != nullptr)
    {
        tsqDeallocateContextCompression_MT(context);
        return 0;
    }
    else
    {
        return 1;
    }
}


int test_tsq_compress_mt()
{
    char *compressed1 = nullptr;
    char *compressed2 = nullptr;
    char *compressed3 = nullptr;
    size_t compressed_sz1 = 0;
    size_t compressed_sz2 = 0;
    size_t compressed_sz3 = 0;

    TSQCompressionContext_MT* context = tsqAllocateContextCompression_MT( true );

    if (context != nullptr)
    {
        // We try to compress the same input with different settings
        tsqCompress_MT(context, (uint8_t*) testinput, strlen(testinput), false, (uint8_t**) &compressed1, &compressed_sz1, false, false, 0);
        tsqCompress_MT(context, (uint8_t*) testinput, strlen(testinput), false, (uint8_t**) &compressed2, &compressed_sz2, false, true, 0);
        tsqCompress_MT(context, (uint8_t*) testinput, strlen(testinput), false, (uint8_t**) &compressed3, &compressed_sz3, false, true, 3);

        tsqDeallocateContextCompression_MT(context);
        return 0;
    }
    else
    {
        return 1;
    }
}


int test_tsq_context_mt2()
{
    TSQDecompressionContext_MT* context = tsqAllocateContextDecompression_MT( false );

    if (context != nullptr)
    {
        tsqDeallocateContextDecompression_MT(context);
        return 0;
    }
    else
    {
        return 1;
    }
}


int test_tsq_decompress_mt()
{
    char *compressed1 = nullptr;
    char *compressed2 = nullptr;
    char *compressed3 = nullptr;
    size_t compressed_sz1 = 0;
    size_t compressed_sz2 = 0;
    size_t compressed_sz3 = 0;
    char *decompressed1 = nullptr;
    char *decompressed2 = nullptr;
    char *decompressed3 = nullptr;
    size_t decompressed_sz1 = 0;
    size_t decompressed_sz2 = 0;
    size_t decompressed_sz3 = 0;

    TSQCompressionContext_MT* context = tsqAllocateContextCompression_MT( true );
    TSQDecompressionContext_MT* decontext = tsqAllocateContextDecompression_MT( true );

    if (context != nullptr)
    {
        // We try to compress the same input with different settings
        tsqCompress_MT( context, (uint8_t*) testinput, strlen(testinput), false, (uint8_t**) &compressed1, &compressed_sz1, false, false, 0 );
        tsqCompress_MT( context, (uint8_t*) testinput, strlen(testinput), false, (uint8_t**) &compressed2, &compressed_sz2, false, true, 0 );
        tsqCompress_MT( context, (uint8_t*) testinput, strlen(testinput), false, (uint8_t**) &compressed3, &compressed_sz3, false, true, 3 );

        tsqDecompress_MT( decontext, (uint8_t*) compressed1, compressed_sz1, false, (uint8_t**) &decompressed1, &decompressed_sz1, false );
        tsqDecompress_MT( decontext, (uint8_t*) compressed2, compressed_sz2, false, (uint8_t**) &decompressed2, &decompressed_sz2, false );
        tsqDecompress_MT( decontext, (uint8_t*) compressed3, compressed_sz3, false, (uint8_t**) &decompressed3, &decompressed_sz3, false );

        tsqDeallocateContextCompression_MT(context);
        tsqDeallocateContextDecompression_MT(decontext);

        uint32_t retval = 0;

        if (strncmp(testinput, decompressed1, 700) != 0 || strncmp(testinput, decompressed2, 700) != 0 || strncmp(testinput, decompressed3, 700) != 0)
            retval = 1;

        return retval;
    }
    else
    {
        return 1;
    }
}


int main( int argc, const char** argv )
{
    int status = -1;

    if (argc != 2) return -2;

    if (strcmp(argv[1], "test_tsq_context") == 0)
        status = test_tsq_context();
    if (strcmp(argv[1], "test_tsq_compress") == 0)
        status = test_tsq_compress();
    if (strcmp(argv[1], "test_tsq_context_mt") == 0)
        status = test_tsq_context_mt();
    if (strcmp(argv[1], "test_tsq_compress_mt") == 0)
        status = test_tsq_compress_mt();
    if (strcmp(argv[1], "test_tsq_context_mt2") == 0)
        status = test_tsq_context_mt2();
    if (strcmp(argv[1], "test_tsq_decompress_mt") == 0)
        status = test_tsq_decompress_mt();

    return status;
}
