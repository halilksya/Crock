#include "mrock/game/nbt.h"

//Every value starts with its type byte and name, the name length being a varuint32 | Her değer tip baytı ve ismiyle başlıyor, ismin uzunluğu da varuint32
static void write_tag_header(bs_t *stream, uint8_t tag_type, const char *name)
{
    bs_wu8(stream, tag_type);
    bs_wstr_var(stream, name);
}

void mc_nbt_compound_start(bs_t *stream, const char *name)
{
    write_tag_header(stream, MC_NBT_TAG_COMPOUND, name);
}

void mc_nbt_compound_end(bs_t *stream)
{
    bs_wu8(stream, MC_NBT_TAG_END);
}

//Comes out as the three bytes 0A 00 00 | Çıktısı 0A 00 00 olmak üzere üç bayt
void mc_nbt_empty_compound(bs_t *stream)
{
    mc_nbt_compound_start(stream, "");
    mc_nbt_compound_end(stream);
}

void mc_nbt_empty_list(bs_t *stream, const char *name, uint8_t element_type)
{
    write_tag_header(stream, MC_NBT_TAG_LIST, name);
    bs_wu8(stream, element_type);
    //The element count is an NBT int32, so zigzag varint and not plain | Eleman sayısı NBT int32'si, yani zigzag varint, düz değil
    bs_wvari32(stream, 0);
}
