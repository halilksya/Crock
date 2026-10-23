#ifndef MCROCK_RAKNET_FRAME_H
#define MCROCK_RAKNET_FRAME_H

#include "mrock/raknet/proto.h"
#include "mrock/util/bstream.h"

//Caps the largest message a split can carry | Bir split'in taşıyabileceği en büyük mesajı sınırlar
#define RAKNET_MAX_SPLIT_PART_COUNT 128

#define RAKNET_MAX_CONCURRENT_SPLITS 4

#define RAKNET_MAX_FRAME_PER_DATAGRAM 128

typedef struct
{
    uint8_t reliability;

    //Which of these is on the wire depends on reliability, see RAKNET_IS_* | Bunlardan hangileri yazılıyor reliability'ye göre değişiyor, kararı RAKNET_IS_* makroları veriyor
    //24-bit on the wire, they wrap at 0xFFFFFF: compare with triad_* | Wire'da 24 bit; 0xFFFFFF'ten sonra sıfıra dönüyor, o yüzden < ile değil triad_* ile karşılaştır
    uint32_t reliable_index;
    uint32_t sequenced_index;
    uint32_t ordered_index;
    uint8_t order_channel;

    bool is_split;
    uint32_t split_count;
    uint16_t split_id;
    uint32_t split_index;

    //After decode this points into the source datagram, no copy. Call own_payload to keep it | Decode kopyalamıyor, gelen datagramın içine işaret ediyor. O datagram bir sonraki recvfrom'da eziliyor; frame'i saklayacaksan own_payload çağır
    uint8_t *payload;
    //uint32 because reassemble splits exceed 65535; a single wire frame is capped at 8191 | Birleştirilmiş split 64 KB'ı geçebiliyor, o yüzden uint32. Tek bir frame zaten 8191 baytta kalıyor, wire'daki uzunluk alanı bit cinsinden ve 16 bitlik
    uint32_t payload_len;
    bool owns_payload;
} raknet_frame_t;

void raknet_frame_decode(bs_t *stream, raknet_frame_t *frame);

void raknet_frame_encode(bs_t *stream, const raknet_frame_t *frame);

size_t raknet_frame_size(const raknet_frame_t *frame);

size_t raknet_frame_header_size(uint8_t reliability, bool is_split);

//Copies the payload to the help heap so it outlives the datagram | Payload'ı heap'e kopyalıyor; datagram gitse de frame yaşıyor
int raknet_frame_own_payload(raknet_frame_t *frame);

void raknet_frame_free(raknet_frame_t *frame);

#endif