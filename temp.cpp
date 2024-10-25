#include <iostream>
#include <fstream>
#include <sstream>
#include <zlib.h>
#include <string.h>
#include <iomanip>

std::string compress_string(const std::string &str, int compression_level = Z_BEST_COMPRESSION)
{
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (deflateInit2(&zs, compression_level, Z_DEFLATED, 15 | 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
    {
        throw std::runtime_error("deflateInit2 failed.");
    }

    zs.next_in = (Bytef *)str.data();
    zs.avail_in = str.size();
    int ret;
    char outbuffer[32768];
    std::string outstring;

    do
    {
        zs.next_out = reinterpret_cast<Bytef *>(outbuffer);
        zs.avail_out = sizeof(outbuffer);
        ret = deflate(&zs, Z_FINISH);

        if (outstring.size() < zs.total_out)
        {
            outstring.append(outbuffer, zs.total_out - outstring.size());
        }
    } while (ret == Z_OK);

    deflateEnd(&zs);

    if (ret != Z_STREAM_END)
    {
        throw std::runtime_error("Exception during zlib compression.");
    }

    return outstring;
}

std::string decompress_string(const std::string &str)
{
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (inflateInit2(&zs, 15 | 16) != Z_OK)
    {
        throw std::runtime_error("inflateInit2 failed.");
    }

    zs.next_in = (Bytef *)str.data();
    zs.avail_in = str.size();
    int ret;
    char outbuffer[32768];
    std::string outstring;

    do
    {
        zs.next_out = reinterpret_cast<Bytef *>(outbuffer);
        zs.avail_out = sizeof(outbuffer);
        ret = inflate(&zs, 0);

        if (outstring.size() < zs.total_out)
        {
            outstring.append(outbuffer, zs.total_out - outstring.size());
        }
    } while (ret == Z_OK);

    inflateEnd(&zs);

    if (ret != Z_STREAM_END)
    {
        throw std::runtime_error("Exception during zlib decompression.");
    }

    return outstring;
}

int main()
{
    std::string data;
    std::cout << "Enter a string: ";
    std::cin >> data;

    std::string compressed_data = compress_string(data);
    std::cout << "Compressed (hex):" << std::endl;

    // Print the compressed data in specified hex format
    int count = 0;
    for (unsigned char c : compressed_data)
    {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)c << " ";
        count++;

        // Newline every 8 bytes
        if (count % 8 == 0)
        {
            std::cout << std::endl;
        }
    }
    if (count % 8 != 0)
    {
        std::cout << std::endl; // Final newline if the last line was incomplete
    }

    // Decompress and print the original and decompressed strings
    std::string decompressed_data = decompress_string(compressed_data);
    std::cout << "Original: " << data << std::endl;
    std::cout << "Compressed size: " << compressed_data.size() << std::endl;
    std::cout << "Decompressed: " << decompressed_data << std::endl;

    return 0;
}
