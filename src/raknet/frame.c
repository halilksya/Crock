#include "mrock/raknet/frame.h"

#include <string.h>
#include <stdlib.h>

size_t raknet_frame_header_size(uint8_t reliability, bool is_split)
{
    //Flags byte plus the 16-bit length field | Flags baytı artı 16 bitlik uzunluk alanı
    size_t size = 1 + 2;

    //Each index is a 3-byte triad | Her indeks 3 baytlık triad
    if(RAKNET_IS_RELIABLE(reliability))
        size += 3;

    if(RAKNET_IS_SEQUENCED(reliability))
        size += 3;

    //Ordered carries a triad and a channel byte | Ordered bir triad ve bir kanal baytı taşıyor
    if(RAKNET_IS_ORDERED(reliability))
        size += 3 + 1;

    //Split count, id and index | Split sayısı, id'si ve indeksi
    if(is_split)
        size += 4 + 2 + 4;

    return size;
}

size_t raknet_frame_size(const raknet_frame_t *frame)
{
    return raknet_frame_header_size(frame -> reliability, frame -> is_split) + frame -> payload_len;
}

void raknet_frame_decode(bs_t *stream, raknet_frame_t *frame)
{
    memset(frame, 0, sizeof(*frame));

    //Reliability sits in the top three bits of the flags byte | Reliability flags baytının üst üç bitinde
    uint8_t flags = bs_ru8(stream);
    frame -> reliability = (uint8_t)(flags >> 5);
    frame -> is_split = (flags & RAKNET_FLAG_SPLIT) != 0;

    //The length is in bits, not bytes, so round up when converting | Uzunluk bit cinsinden, bayt değil, o yüzden yukarı yuvarlıyoruz
    uint16_t bit_len = bs_ru16be(stream);
    size_t byte_len = ((size_t)bit_len + 7) / 8;

    //A hostile peer could claim any length here | Kötü niyetli bir karşı taraf burada her uzunluğu iddia edebilir
    if(byte_len == 0 || byte_len > RAKNET_MAX_MTU)
    {
        stream -> err = 1;
        return;
    }
    frame -> payload_len = (uint32_t)byte_len;

    if(RAKNET_IS_RELIABLE(frame -> reliability))
        frame -> reliable_index = bs_ru24le(stream);

    if(RAKNET_IS_SEQUENCED(frame -> reliability))
        frame -> sequenced_index = bs_ru24le(stream);

    if(RAKNET_IS_ORDERED(frame -> reliability))
    {
        frame -> ordered_index = bs_ru24le(stream);
        frame -> order_channel = bs_ru8(stream);

        //The channel indexes arrays later on, so bound it here | Kanal ilerde dizileri indeksliyor, sınırı burada koyuyoruz
        if(frame -> order_channel >= RAKNET_MAX_ORDER_CHANNELS)
        {
            stream -> err = 1;
            return;
        }
    }

    if(frame -> is_split)
    {
        frame -> split_count = bs_ru32be(stream);
        frame -> split_id = bs_ru16be(stream);
        frame -> split_index = bs_ru32be(stream);

        //The index guard keeps reassembly from writing past the parts array | İndeks kontrolü, birleştirmenin parts dizisinin dışına yazmasını engelliyor
        if(frame -> split_count == 0 || frame -> split_count > RAKNET_MAX_SPLIT_PART_COUNT || frame -> split_index >= frame -> split_count)
        {
            stream -> err = 1;
            return;
        }
    }

    //Points into the stream buffer, the const is dropped because own_payload may replace it | Stream buffer'ının içini gösteriyor, const düşüyor çünkü own_payload bunu değiştirebilir
    frame -> payload = (uint8_t *)bs_rbytes(stream, byte_len);
    frame -> owns_payload = false;

    if(!frame -> payload)
        stream -> err = 1;
}

void raknet_frame_encode(bs_t *stream, const raknet_frame_t *frame)
{
    uint8_t flags = (uint8_t)(frame -> reliability << 5);
    if(frame -> is_split)
        flags |= RAKNET_FLAG_SPLIT;

    //The length field holds bits in 16 bits, so 8191 bytes is the ceiling | Uzunluk alanı 16 bitte bit tutuyor, yani tavan 8191 bayt
    if(frame -> payload_len == 0 || frame -> payload_len > 8191)
    {
        stream -> err = 1;
        return;
    }

    bs_wu8(stream, flags);
    bs_wu16be(stream, (uint16_t)(frame -> payload_len * 8));

    if(RAKNET_IS_RELIABLE(frame -> reliability))
        bs_wu24le(stream, frame -> reliable_index);

    if(RAKNET_IS_SEQUENCED(frame -> reliability))
        bs_wu24le(stream, frame -> sequenced_index);

    if(RAKNET_IS_ORDERED(frame -> reliability))
    {
        bs_wu24le(stream, frame -> ordered_index);
        bs_wu8(stream, frame -> order_channel);
    }

    if(frame -> is_split)
    {
        bs_wu32be(stream, frame -> split_count);
        bs_wu16be(stream, frame -> split_id);
        bs_wu32be(stream, frame -> split_index);
    }

    bs_wbytes(stream, frame -> payload, frame -> payload_len);
}

int raknet_frame_own_payload(raknet_frame_t *frame)
{
    if(frame -> owns_payload)
        return 0;

    if(frame -> payload_len == 0)
    {
        frame -> payload = NULL;
        frame -> owns_payload = false;
        return 0;
    }

    uint8_t *copy = (uint8_t *)malloc(frame -> payload_len);
    if(!copy)
        return -1;

    memcpy(copy, frame -> payload, frame -> payload_len);
    frame -> payload = copy;
    frame -> owns_payload = true;
    return 0;
}

void raknet_frame_free(raknet_frame_t *frame)
{
    if(frame -> owns_payload)
        free(frame -> payload);

    frame -> payload = NULL;
    frame -> payload_len = 0;
    frame -> owns_payload = false;
}
