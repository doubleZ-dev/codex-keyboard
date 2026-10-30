#include "maintenance_frame.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t received[MAINTENANCE_JSON_MAX];
static size_t received_length;
static unsigned messages;
static void receive(const uint8_t *bytes,size_t length,void *ctx)
{ (void)ctx; ++messages; memcpy(received,bytes,length); received_length=length; }
int main(int argc,char **argv)
{
    uint8_t payload[MAINTENANCE_JSON_MAX],frame[MAINTENANCE_FRAME_MAX];
    maintenance_rx_t rx={0};
    for (unsigned n=1;n<=MAINTENANCE_JSON_MAX;n+=17) {
        for (unsigned i=0;i<n;++i) payload[i]=(i*37+n)&255;
        size_t length=maintenance_encode(payload,n,frame,sizeof(frame)); assert(length);
        unsigned before=messages;
        for (size_t i=0;i<length;++i) maintenance_feed(&rx,frame+i,1,receive,NULL);
        assert(messages==before+1 && received_length==n && !memcmp(payload,received,n));
    }
    memset(payload,255,sizeof(payload));
    size_t length=maintenance_encode(payload,sizeof(payload),frame,sizeof(frame)); assert(length<=sizeof(frame));
    maintenance_feed(&rx,(const uint8_t *)"noise logs\n",11,receive,NULL);
    maintenance_feed(&rx,frame,length,receive,NULL); assert(received_length==sizeof(payload));
    unsigned before=messages; frame[10]^=1;
    maintenance_feed(&rx,frame,length,receive,NULL); assert(messages==before);
    uint8_t bad[MAINTENANCE_FRAME_MAX+100]; memset(bad,1,sizeof(bad));
    maintenance_feed(&rx,bad,sizeof(bad),receive,NULL);
    maintenance_feed(&rx,frame,length,receive,NULL); assert(messages==before);
    frame[10]^=1; maintenance_feed(&rx,frame,length,receive,NULL); assert(messages==before+1);
    assert(!maintenance_encode(payload,0,frame,sizeof(frame)));
    /* Used by Python to compare both real implementations byte for byte. */
    if (argc==2) {
        FILE *f=fopen(argv[1],"wb"); assert(f); assert(fwrite(frame,1,length,f)==length); fclose(f);
    }
    puts("CDC framing: binary/zero/255 boundaries, bytewise fragments, maximum frame, noise, CRC damage, overflow and resynchronization passed");
}
