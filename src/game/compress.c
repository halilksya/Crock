//Must come before zlib.h; it makes next_in const so src can be assigned without a cast | zlib.h'den önce gelmeli; next_in'i const yapıyor, böylece src cast'siz atanabiliyor
#define ZLIB_CONST
#include "mrock/game/compress.h"

#include <zlib.h>
#include <string.h>

#define INFLATE_CHUNK 16384

int mc_compress_inflate(const uint8_t *src, size_t src_len, bs_t *out, size_t max_out)
{
    if(src_len > UINT32_MAX)
        return -1;

    size_t start = out -> pos;
    z_stream stream;
    memset(&stream, 0, sizeof(stream));

    //Negative windowBits means raw deflate with no zlib header | Negatif windowBits, zlib başlığı olmayan ham deflate demek
    if(inflateInit2(&stream, -15) != Z_OK)
        return -1;

    stream.next_in =  src;
    stream.avail_in = (uInt)src_len;

    int result = Z_OK;

    while(result != Z_STREAM_END)
    {
        //Compression bomb guard | Sıkıştırma bombası koruması
        size_t produced = out -> pos - start;
        if(produced >= max_out)
            goto fail;

        size_t chunk = INFLATE_CHUNK;
        if(produced + chunk > max_out)
            chunk = max_out - produced;

        uint8_t *dst = bs_wreserve(out, chunk);
        if(!dst)
            goto fail;

        stream.next_out = dst;
        stream.avail_out = (uInt)chunk;

        result = inflate(&stream, Z_NO_FLUSH);

        //We reserved a whole chunk, so rewind by whatever zlib left unwritten | Bütün chunk'ı ayırdık, zlib'in yazmadığı kadarını geri alıyoruz
        out -> pos -= stream.avail_out;
        out -> len = out -> pos;

        if(result != Z_OK && result != Z_STREAM_END)
            goto fail;

        //Input ran out with room left and no end marker, so the stream is truncated | Girdi bitti ama yer vardı ve bitiş işareti gelmedi, yani stream yarım
        if(result == Z_OK && stream.avail_in == 0 && stream.avail_out != 0)
            goto fail;
    }

    inflateEnd(&stream);
    return 0;

//On failure the stream is rewound, so the caller sees out untouched | Hata halinde stream geri alınıyor, çağıran out'u hiç dokunulmamış görüyor
fail:
    inflateEnd(&stream);
    out -> pos = start;
    out -> len = start;
    return -1;
}

int mc_compress_deflate(const uint8_t *src, size_t src_len, bs_t *out)
{
    if(src_len > UINT32_MAX)
        return -1;

    size_t start = out -> pos;
    z_stream stream;
    memset(&stream, 0, sizeof(stream));

    if(deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return -1;

    //Worst-case output size, so one reserve is enough and no loop is needed | En kötü durum çıktı boyu, bu yüzden tek ayırma yetiyor ve döngüye gerek kalmıyor
    uLong bound = deflateBound(&stream, (uLong)src_len);

    uint8_t *dst = bs_wreserve(out, bound);
    if(!dst)
    {
        deflateEnd(&stream);
        return -1;
    }

    stream.next_in = src;
    stream.avail_in = (uInt)src_len;
    stream.next_out = dst;
    stream.avail_out = (uInt)bound;

    int result = deflate(&stream, Z_FINISH);

    out -> pos -= stream.avail_out;
    out -> len = out -> pos;

    deflateEnd(&stream);

    if(result != Z_STREAM_END)
    {
        out -> pos = start;
        out -> len = start;
        return -1;
    }
    return 0;
}
