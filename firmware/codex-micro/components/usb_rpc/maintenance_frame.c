#include "maintenance_frame.h"
#include <string.h>
static uint32_t crc(const uint8_t *bytes,size_t n)
{
    uint32_t c=0xffffffff;
    while (n--) { c^=*bytes++; for (unsigned i=0;i<8;++i) c=(c>>1)^(0xedb88320U & (0U-(c&1))); }
    return ~c;
}
size_t maintenance_encode(const uint8_t *json,size_t length,uint8_t *out,size_t capacity)
{
    if (!json || !length || length>MAINTENANCE_JSON_MAX || !out || capacity<MAINTENANCE_FRAME_MAX) return 0;
    uint8_t raw[MAINTENANCE_JSON_MAX+11];
    memcpy(raw,"CMUP",4); raw[4]=1; raw[5]=length; raw[6]=length>>8;
    memcpy(raw+7,json,length);
    uint32_t checksum=crc(raw,length+7);
    for (unsigned i=0;i<4;++i) raw[length+7+i]=checksum>>(8*i);
    out[0]=0; size_t code_pos=1,pos=2; uint8_t code=1;
    for (size_t i=0;i<length+11;++i) {
        if (!raw[i]) { out[code_pos]=code; code_pos=pos++; code=1; }
        else { out[pos++]=raw[i]; if (++code==255) { out[code_pos]=code; code_pos=pos++; code=1; } }
    }
    out[code_pos]=code; out[pos++]=0; return pos;
}
void maintenance_reset(maintenance_rx_t *s)
{ s->length=0; s->collecting=s->overflow=false; }
static void decode(maintenance_rx_t *s,maintenance_message_fn receive,void *ctx)
{
    /* Decode in place: the output cursor never overtakes the encoded cursor. */
    size_t read=0,write=0;
    while (read<s->length) {
        unsigned code=s->bytes[read++];
        if (!code || code-1>s->length-read) goto invalid;
        for (unsigned i=1;i<code;++i) s->bytes[write++]=s->bytes[read++];
        if (code<255 && read<s->length) s->bytes[write++]=0;
    }
    if (write<12 || memcmp(s->bytes,"CMUP",4) || s->bytes[4]!=1) goto invalid;
    size_t payload=s->bytes[5]|((size_t)s->bytes[6]<<8);
    if (!payload || payload>MAINTENANCE_JSON_MAX || write!=payload+11) goto invalid;
    uint32_t expected=0;
    for (unsigned i=0;i<4;++i) expected|=(uint32_t)s->bytes[payload+7+i]<<(8*i);
    if (crc(s->bytes,payload+7)!=expected) goto invalid;
    ++s->accepted; receive(s->bytes+7,payload,ctx); return;
invalid:
    ++s->rejected;
}
void maintenance_feed(maintenance_rx_t *s,const uint8_t *bytes,size_t length,maintenance_message_fn receive,void *ctx)
{
    for (size_t i=0;i<length;++i) {
        if (!bytes[i]) {
            if (s->collecting && s->length && !s->overflow) decode(s,receive,ctx);
            else if (s->overflow) ++s->rejected;
            s->collecting=true; s->length=0; s->overflow=false;
        } else if (s->collecting && !s->overflow) {
            if (s->length<sizeof(s->bytes)) s->bytes[s->length++]=bytes[i];
            else s->overflow=true;
        }
    }
}
