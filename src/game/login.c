#include "mrock/game/login.h"
#include "mrock/game/base64.h"

#include "cjson/cJSON.h"

#include <stdlib.h>
#include <string.h>

//Enough for the chain JWT, but not for the client data one, which carries skin images | Chain JWT'si için yeterli, ama skin görüntüsü taşıyan client data JWT'si için değil
#define MAX_JWT_PAYLOAD 8192

//A JWT is header.payload.signature; we take the middle part and ignore the signature | JWT header.payload.signature şeklinde; ortadaki kısmı alıyoruz, imzaya bakmıyoruz
static int decode_jwt_payload(const char *jwt, char *out, size_t out_cap)
{
    const char *first_dot = strchr(jwt, '.');
    if(!first_dot)
        return -1;

    const char *second_dot = strchr(first_dot + 1, '.');
    if(!second_dot)
        return -1;

    const char *payload = first_dot + 1;
    size_t payload_len = (size_t)(second_dot - payload);

    if(payload_len == 0 || MC_BASE64_DECODED_SIZE(payload_len) >= out_cap)
        return -1;

    int decoded = mc_base64url_decode(payload, payload_len, (uint8_t *)out, out_cap - 1);
    if(decoded < 0)
        return -1;

    out[decoded] = '\0';
    return decoded;
}

static bool copy_string_field(const cJSON *object, const char *field, char *out, size_t out_cap)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, field);
    if(!cJSON_IsString(item) || !item -> valuestring)
        return false;

    size_t len = strlen(item -> valuestring);
    if(len >= out_cap)
        len = out_cap - 1;

    memcpy(out, item -> valuestring, len);
    out[len] = '\0';
    return true;
}

static int extract_identity(const char *payload_json, mc_player_identity_t *identity)
{
    cJSON *root = cJSON_Parse(payload_json);
    if(!root)
        return -1;

    //New PlayFab format keeps the identity at the root level | Yeni PlayFab formatı kimliği kök seviyede tutuyor
    const cJSON *xname = cJSON_GetObjectItemCaseSensitive(root, "xname");
    if(cJSON_IsString(xname))
    {
        bool ok = copy_string_field(root, "xname", identity -> name, sizeof(identity -> name));

        copy_string_field(root, "xid", identity -> xuid, sizeof(identity -> xuid));
        copy_string_field(root, "sub", identity -> uuid, sizeof(identity -> uuid));

        cJSON_Delete(root);
        return ok ? 0 : -1;
    }

    //Older clients nest it under extraData with different field names | Eski client'lar extraData altında, farklı alan adlarıyla gönderiyor
    const cJSON *extra = cJSON_GetObjectItemCaseSensitive(root, "extraData");
    if(!cJSON_IsObject(extra))
    {
        cJSON_Delete(root);
        return -1;
    }

    bool ok = copy_string_field(extra, "displayName", identity -> name, sizeof(identity -> name));

    ok = copy_string_field(extra, "identity", identity -> uuid, sizeof(identity -> uuid)) && ok;

    copy_string_field(extra, "XUID", identity -> xuid, sizeof(identity -> xuid));

    cJSON_Delete(root);
    return ok ? 0 : -1;
}

static int parse_chain(const char *chain_json, size_t chain_len, mc_player_identity_t *identity)
{
    cJSON *root = cJSON_ParseWithLength(chain_json, chain_len);
    if(!root)
        return -1;

    const char *jwt = NULL;

    //New format is a single token object | Yeni format tek bir token nesnesi
    const cJSON *token = cJSON_GetObjectItemCaseSensitive(root, "Token");
    if(cJSON_IsString(token) && token -> valuestring)
    {
        jwt = token -> valuestring;
    }
    else
    {
        //Old format is an array, and the identity sits in its last element | Eski format bir dizi, kimlik de son elemanında duruyor
        const cJSON *chain = cJSON_GetObjectItemCaseSensitive(root, "chain");
        if(!cJSON_IsArray(chain))
        {
            cJSON_Delete(root);
            return -1;
        }

        int count = cJSON_GetArraySize(chain);
        if(count < 1)
        {
            cJSON_Delete(root);
            return -1;
        }

        const cJSON *last = cJSON_GetArrayItem(chain, count - 1);
        if(!cJSON_IsString(last) || !last -> valuestring)
        {
            cJSON_Delete(root);
            return -1;
        }

        jwt = last -> valuestring;
    }

    char payload[MAX_JWT_PAYLOAD];
    int decoded = decode_jwt_payload(jwt, payload, sizeof(payload));
    if(decoded < 0)
    {
        cJSON_Delete(root);
        return -1;
    }

    int result = extract_identity(payload, identity);
    cJSON_Delete(root);
    return result;
}

int mc_login_parse(const uint8_t *body, size_t body_len, mc_player_identity_t *identity)
{
    memset(identity, 0, sizeof(*identity));

    bs_t stream;
    bs_read_init(&stream, body, body_len);

    //The protocol version again; RequestNetworkSettings already checked it | Protokol sürümü tekrar geliyor; RequestNetworkSettings bunu zaten kontrol etti
    (void)bs_ru32be(&stream);

    //Outer length is a varint, but the two lengths inside are fixed uint32 LE | Dıştaki uzunluk varint, ama içindeki iki uzunluk sabit uint32 LE
    uint32_t inner_len = bs_rvaru32(&stream);
    if(stream.err || inner_len == 0)
        return -1;

    const uint8_t *inner = bs_rbytes(&stream, inner_len);
    if(!inner)
        return -1;

    bs_t inner_stream;
    bs_read_init(&inner_stream, inner, inner_len);

    uint32_t chain_len = bs_ru32le(&inner_stream);
    if(inner_stream.err || chain_len == 0)
        return -1;

    const uint8_t *chain_json = bs_rbytes(&inner_stream, chain_len);
    if(!chain_json)
        return -1;

    //Client data holds the skin, device and language; skipped for now but needed later | Client data skin, cihaz ve dil bilgisini taşıyor; şimdilik atlanıyor ama sonra gerekecek
    uint32_t client_data_len = bs_ru32le(&inner_stream);
    if(inner_stream.err || client_data_len == 0)
        return -1;

    if(!bs_rbytes(&inner_stream, client_data_len))
        return -1;

    return parse_chain((const char *)chain_json, chain_len, identity);
}
