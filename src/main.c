#include "mrock/common.h"
#include "mrock/raknet/proto.h" // Geçici, derleniyormu bakmak için.

int main(void)
{
    log_set_level(LOG_DEBUG);
    log_info("mrock başladı, saat: %llu", (unsigned long long)mr_now_ms());

    uint8_t magic[] = { 0x00, 0xff, 0xff, 0x00, 0xfe, 0xfe, 0xfe, 0xfe,0xfd, 0xfd, 0xfd, 0xfd, 0x12, 0x34, 0x56, 0x78 };
    log_hexdump(LOG_DEBUG, "raknet magic", magic, sizeof(magic));

    return 0;
}