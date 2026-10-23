#include "mrock/raknet/ack.h"

//One type byte plus a 3-byte triad, or two triads for a range | Bir tip baytı artı 3 baytlık triad, aralıkta iki triad
#define ACK_RECORD_SINGLE_SIZE 4
#define ACK_RECORD_RANGE_SIZE 7

//Flags byte plus the uint16 record count | Flags baytı artı uint16 kayıt sayacı
#define ACK_HEADER_SIZE 3

size_t raknet_ack_encode(bs_t *stream, const uint32_t *sequences, size_t count, bool is_ack, size_t mtu)
{
    if(count == 0)
        return 0;

    uint8_t flags = RAKNET_FLAG_VALID | (is_ack ? RAKNET_FLAG_ACK : RAKNET_FLAG_NAK);
    bs_wu8(stream, flags);

    //The record count is only known once the loop ends, so reserve room and backfill it | Kayıt sayısı döngü bitmeden bilinmiyor, o yüzden yer ayırıp sonradan dolduruyoruz
    //We keep the offset rather than the pointer, since a later write could realloc | Pointer değil offset saklıyoruz, çünkü sonraki bir yazma realloc yapabilir
    size_t count_offset = stream->pos;

    if(!bs_wreserve(stream, 2))
        return 0;

    size_t used = ACK_HEADER_SIZE;
    size_t consumed = 0;
    uint16_t record_count = 0;

    while(consumed < count)
    {
        //Collapse a consecutive run into one range record; this only works on sorted input | Ardışık diziyi tek bir aralık kaydına indiriyor; bu sadece sıralı girdide çalışıyor
        size_t run_end = consumed;
        while(run_end + 1 < count && sequences[run_end + 1] == triad_inc(sequences[run_end]))
            run_end++;

        bool is_range = (run_end > consumed);
        size_t need = is_range ? ACK_RECORD_RANGE_SIZE : ACK_RECORD_SINGLE_SIZE;

        //Out of room, the caller sends the rest in another datagram | Yer kalmadı, çağıran kalanı başka bir datagramda gönderiyor
        if(used + need > mtu)
            break;

        if(is_range)
        {
            bs_wu8(stream, 0);   //0 marks a range
            bs_wu24le(stream, sequences[consumed]);
            bs_wu24le(stream, sequences[run_end]);
        }
        else
        {
            bs_wu8(stream, 1);   //1 marks a single value
            bs_wu24le(stream, sequences[consumed]);
        }

        if(stream->err)
            return 0;

        used += need;
        record_count++;
        consumed = run_end + 1;
    }

    stream -> buf[count_offset] = (uint8_t)(record_count >> 8);
    stream -> buf[count_offset + 1] = (uint8_t)record_count;

    //Sequences consumed, not bytes written | Tüketilen sequence sayısı, yazılan bayt değil
    return consumed;
}

size_t raknet_ack_decode(bs_t *stream, uint32_t *out_sequences, size_t out_capacity)
{
    uint16_t record_count = bs_ru16be(stream);
    size_t written = 0;

    for(uint16_t i = 0; i < record_count; i++)
    {
        if(stream -> err)
            break;

        uint8_t is_single = bs_ru8(stream);

        if(is_single)
        {
            uint32_t sequence = bs_ru24le(stream);
            if(stream -> err)
                break;

            if(written < out_capacity)
                out_sequences[written++] = sequence;
            continue;
        }

        uint32_t start = bs_ru24le(stream);
        uint32_t end = bs_ru24le(stream);
        if(stream -> err)
            break;

        //A peer could claim a range covering the whole 24-bit space, so bound it | Karşı taraf 24 bitlik alanın tamamını kapsayan bir aralık iddia edebilir, sınırlıyoruz
        int32_t span = triad_diff(end, start);
        if(span < 0 || (size_t)span >= RAKNET_MAX_ACK_SEQUENCES)
        {
            stream -> err = 1;
            break;
        }

        uint32_t sequence = start;
        for(int32_t j = 0; j <= span; j++)
        {
            if(written >= out_capacity)
                break;
            out_sequences[written++] = sequence;
            sequence = triad_inc(sequence);
        }
    }
    return written;
}
