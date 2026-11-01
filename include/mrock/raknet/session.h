#ifndef MROCK_RAKNET_SESSION_H
#define MROCK_RAKNET_SESSION_H

#include "mrock/raknet/proto.h"
#include "mrock/raknet/frame.h"
#include "mrock/net/addr.h"
#include "mrock/game/connection.h"

//Must be a multiple of 8, the seen bitmaps are sized by dividing it | 8'in katı olmak zorunda, seen bitmap'leri buna bölünerek boyutlanıyor
#define RAKNET_RECV_WINDOW 2048
//Must stay a power of two, cache_slot masks with CAPACITY - 1 | İkinin kuvveti kalmak zorunda, cache_slot CAPACITY - 1 ile maskeliyor
#define RAKNET_SEND_CACHE_CAPACITY 2048
#define RAKNET_SEND_QUEUE_CAPACITY 512
#define RAKNET_ORDER_BUFFER_CAPACITY 512
#define RAKNET_SESSION_TIMEOUT_MS 10000
#define RAKNET_PING_INTERVAL_MS 5000
//Fixed timeout, there is no RTT estimation yet | Sabit süre, henüz RTT tahmini yok
#define RAKNET_RESEND_TIMEOUT_MS 500
//Resend datagrams per tick, so one timeout scan cannot flood the socket | Tick başına tekrar gönderilen datagram sayısı, tek bir tarama socket'i boğmasın
#define RAKNET_MAX_RESENDS_PER_TICK 32

typedef enum
{
    RAKNET_STATE_CONNECTING = 0,
    RAKNET_STATE_CONNECTED,
    RAKNET_STATE_DISCONNECTED,
    //Destroyed by the server's sweep on the next tick | Sunucunun sonraki tick'teki taramasında yok ediliyor
    RAKNET_STATE_DEAD,
} raknet_session_state_t;

typedef enum
{
    RAKNET_DISCONNECT_CLIENT,
    RAKNET_DISCONNECT_TIMEOUT,
    RAKNET_DISCONNECT_PROTOCOL,
    RAKNET_DISCONNECT_SHUTDOWN,
} raknet_disconnect_reason_t;

typedef struct
{
    //Datagram sequence window, drives ACK, NACK and duplicate detection | Datagram sequence penceresi; ACK, NACK ve duplicate kontrolünü besliyor
    uint32_t window_start;
    uint32_t window_end;
    uint32_t highest_seen;

    uint8_t seen_bits[RAKNET_RECV_WINDOW / 8];

    //Filled while receiving, flushed once per tick | Alırken doluyor, tick başına bir kez boşaltılıyor
    uint32_t ack_queue[RAKNET_RECV_WINDOW];
    size_t ack_count;
    uint32_t nack_queue[RAKNET_RECV_WINDOW];
    size_t nack_count;

    //Separate window over reliable_index, since one datagram can hold several frames | reliable_index üzerinde ayrı bir pencere, çünkü bir datagram birkaç frame taşıyabiliyor
    uint32_t reliable_window_start;
    uint8_t reliable_seen_bits[RAKNET_RECV_WINDOW / 8];

    uint32_t next_ordered[RAKNET_MAX_ORDER_CHANNELS];
    uint32_t highest_sequenced[RAKNET_MAX_ORDER_CHANNELS];
    //Allocated lazily, only for channels that actually arrive out of order | Tembel ayrılıyor, sadece gerçekten sırasız paket gelen kanallar için
    raknet_frame_t *order_buffer[RAKNET_MAX_ORDER_CHANNELS];
    bool *order_slot_used[RAKNET_MAX_ORDER_CHANNELS];
} raknet_recv_layer_t;

//Keeps owned copies of the frames until the ACK arrives | ACK gelene kadar frame'lerin sahipli kopyalarını tutuyor
typedef struct
{
    uint32_t sequence;
    uint64_t sent_at_ms;
    bool in_use;
    raknet_frame_t *frames;
    size_t frame_count;
} raknet_cached_datagram_t;

typedef struct
{
    //mtu is the advertised value, usable_mtu is it minus the UDP header | mtu pazarlanan değer, usable_mtu ondan UDP başlığı çıkarılmış hali
    uint16_t mtu;
    uint16_t usable_mtu;

    uint32_t next_datagram_sequence;
    uint32_t next_reliable_index;
    uint32_t next_ordered[RAKNET_MAX_ORDER_CHANNELS];
    uint32_t next_sequenced[RAKNET_MAX_ORDER_CHANNELS];
    uint16_t next_split_id;

    //Frames waiting to be packed into one datagram | Tek bir datagrama paketlenmeyi bekleyen frame'ler
    raknet_frame_t send_queue[RAKNET_SEND_QUEUE_CAPACITY];
    size_t send_queue_count;
    size_t send_queue_bytes;

    //Indexed by sequence & (CAPACITY - 1), not by position | sequence & (CAPACITY - 1) ile indeksleniyor, sırayla değil
    raknet_cached_datagram_t cache[RAKNET_SEND_CACHE_CAPACITY];

    uint32_t resend_queue[RAKNET_SEND_CACHE_CAPACITY];
    size_t resend_count;
} raknet_send_layer_t;

typedef struct
{
    bool in_use;
    uint16_t split_id;
    uint32_t part_count;
    uint32_t parts_received;
    uint64_t started_ms;

    raknet_frame_t *parts;
    bool *part_present;
} raknet_split_assembly_t;

struct raknet_server;

typedef struct raknet_session
{
    struct raknet_server *server;
    mr_addr_t address;
    uint64_t guid;

    raknet_session_state_t state;

    raknet_recv_layer_t recv;
    raknet_send_layer_t send;

    uint64_t last_recv_ms;
    uint64_t last_ping_ms;
    uint64_t created_ms;

    raknet_split_assembly_t splits[RAKNET_MAX_CONCURRENT_SPLITS];

    uint64_t rx_datagrams;
    uint64_t tx_datagrams;
    uint64_t resends;

    //Chain owned by the server's hash table | Sunucunun hash tablosuna ait zincir
    struct raknet_session *hash_next;
    //Embedded, and its transport pointer points back at this session | Gömülü duruyor, transport pointer'ı da bu session'ı gösteriyor
    mc_connection_t game;
} raknet_session_t;

raknet_session_t *raknet_session_create(struct raknet_server *server, const mr_addr_t *address, uint64_t guid, uint16_t mtu);
void raknet_session_destroy(raknet_session_t *session);
void raknet_session_close(raknet_session_t *session, raknet_disconnect_reason_t reason);

void raknet_session_on_datagram(raknet_session_t *session, const uint8_t *data, size_t len);
void raknet_session_tick(raknet_session_t *session, uint64_t now_ms);

//Copies the payload, so a stack buffer is fine | Payload'ı kopyalıyor, yığın buffer'ı vermek sorun değil
int raknet_send_message(raknet_session_t *session, const uint8_t *payload, size_t len, uint8_t reliability, uint8_t order_channel);
void raknet_send_flush(raknet_session_t *session, uint64_t now_ms);

//Receives one fully reassembled, in-order message | Tamamen birleştirilmiş ve sıraya girmiş tek bir mesaj alıyor
void raknet_session_on_message(raknet_session_t *session, const uint8_t *payload, size_t len);

#endif