#ifndef MCROCK_RAKNET_PROTO_H
#define MCROCK_RAKNET_PROTO_H

#include "mrock/common.h"

//Fixed byte sequence every offline message carries, used to recognise RakNet traffic | Her offline mesajın taşıdığı sabit bayt dizisi, RakNet trafiğini tanımak için
#define RAKNET_MAGIC_LEN 16
extern const uint8_t RAKNET_MAGIC[RAKNET_MAGIC_LEN];

#define RAKNET_PROTOCOL_VERSION 11

#define RAKNET_MIN_MTU 400
#define RAKNET_MAX_MTU 1400

//IPv4 20 + UDP 8. The negotiated MTU includes this, so usable payload is mtu - 28 | IPv4 20 + UDP 8. Pazarlanan MTU bunu içeriyor, yani kullanılabilir alan mtu - 28
#define RAKNET_UDP_HEADER_SIZE 28

#define RAKNET_MAX_ORDER_CHANNELS 32

//Datagram header flags, in the first byte of every datagram | Datagram başlığı bayrakları, her datagramın ilk baytında
#define RAKNET_FLAG_VALID 0x80
#define RAKNET_FLAG_ACK 0x40
#define RAKNET_FLAG_NAK 0x20

//Frame header flag, in the frame's own first byte | Frame başlığı bayrağı, frame'in kendi ilk baytında
#define RAKNET_FLAG_SPLIT 0x10

enum
{
    //Connected messages, these need an open session | Bağlı mesajlar, açık bir session gerektiriyor
    RAKNET_ID_CONNECTED_PING = 0x00,
    RAKNET_ID_CONNECTED_PONG = 0x03,
    RAKNET_ID_CONNECTION_REQUEST = 0x09,
    RAKNET_ID_CONNECTION_REQUEST_ACCEPTED = 0x10,
    RAKNET_ID_NEW_INCOMING_CONNECTION = 0x13,
    RAKNET_ID_DISCONNECTION_NOTIFICATION = 0x15,

    //Offline messages, handled before any session exists | Offline mesajlar, session açılmadan önce işleniyor
    RAKNET_ID_UNCONNECTED_PING = 0x01,
    RAKNET_ID_UNCONNECTED_PING_OPEN_CONNECTIONS = 0x02,
    RAKNET_ID_OPEN_CONNECTION_REQUEST_1 = 0x05,
    RAKNET_ID_OPEN_CONNECTION_REPLY_1 = 0x06,
    RAKNET_ID_OPEN_CONNECTION_REQUEST_2 = 0x07,
    RAKNET_ID_OPEN_CONNECTION_REPLY_2 = 0x08,
    RAKNET_ID_INCOMPATIBLE_PROTOCOL = 0x19,
    RAKNET_ID_UNCONNECTED_PONG = 0x1C,

    //A Minecraft batch arrives under this message ID | Minecraft batch'i bu mesaj ID'siyle geliyor
    RAKNET_ID_GAME_PACKET = 0xFE,
};

//Carried in the top three bits of the frame flags byte | Frame bayrak baytının üst üç bitinde taşınıyor
typedef enum
{
    RAKNET_UNRELIABLE = 0,
    RAKNET_UNRELIABLE_SEQUENCED = 1,
    RAKNET_RELIABLE = 2,
    RAKNET_RELIABLE_ORDERED = 3,
    RAKNET_RELIABLE_SEQUENCED = 4,
    RAKNET_UNRELIABLE_ACK_RECEIPT = 5,
    RAKNET_RELIABLE_ACK_RECEIPT = 6,
    RAKNET_RELIABLE_ORDERED_ACK_RECEIPT = 7,
} raknet_reliability_t;

//Sequenced modes (1 and 4) carry an order channel as well, so they count as ordered too | Sequenced modlar (1 ve 4) order channel da taşıyor, o yüzden ordered sayılıyorlar
#define RAKNET_IS_RELIABLE(reliability) ((reliability) == 2 || (reliability) == 3 || (reliability) == 4 || (reliability) == 6 || (reliability) == 7)
#define RAKNET_IS_SEQUENCED(reliability) ((reliability) == 1 || (reliability) == 4)
#define RAKNET_IS_ORDERED(reliability) ((reliability) == 1 || (reliability) == 3 || (reliability) == 4 || (reliability) == 7)

#endif