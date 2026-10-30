#undef NDEBUG
#include "update_engine.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned begun,written,finished,activated,aborted;
static bool fail_begin,fail_write,fail_finish,fail_activate;
static bool begin(uint32_t size,void *ctx) { (void)ctx; assert(size>=256); ++begun;return !fail_begin; }
static bool write_block(const uint8_t *data,size_t length,void *ctx)
{ (void)ctx;assert(data && length);++written;return !fail_write; }
static bool finish(const uint8_t digest[32],void *ctx)
{ (void)ctx;assert(digest[0]==42);++finished;return !fail_finish; }
static bool activate(void *ctx) { (void)ctx;++activated;return !fail_activate; }
static void abort_write(void *ctx) { (void)ctx;++aborted; }
int main(void)
{
    update_engine_t u; uint8_t digest[32]={42},chunk[1024]={0};
    update_backend_t b={begin,write_block,finish,activate,abort_write,NULL};
    update_engine_init(&u,b);
    assert(!update_engine_begin(&u,0,4096,digest));
    assert(!update_engine_begin(&u,4097,4096,digest));
    assert(!update_engine_begin(&u,256,4096,NULL)); assert(!begun);
    assert(!update_engine_write(&u,0,chunk,1)); assert(!update_engine_activate(&u));
    assert(update_engine_begin(&u,1500,4096,digest));
    assert(!update_engine_begin(&u,256,4096,digest));
    assert(!update_engine_write(&u,1,chunk,10));
    assert(!update_engine_write(&u,0,chunk,1025)); assert(!written);
    assert(update_engine_write(&u,0,chunk,1024)); assert(u.offset==1024);
    assert(!update_engine_write(&u,0,chunk,1024)); /* Lost ACK: no duplicate append. */
    assert(!update_engine_finish(&u)); assert(!finished && !activated);
    assert(!update_engine_write(&u,1024,chunk,477));
    assert(update_engine_write(&u,1024,chunk,476));
    assert(update_engine_finish(&u)); assert(u.state==UPDATE_READY);
    assert(!activated && !update_engine_write(&u,1500,chunk,1));
    assert(update_engine_activate(&u)); assert(u.state==UPDATE_ACTIVATED);
    assert(!update_engine_abort(&u)); assert(!update_engine_activate(&u)); assert(activated==1);
    update_engine_init(&u,b); fail_begin=true;
    assert(!update_engine_begin(&u,256,4096,digest)); assert(u.state==UPDATE_FAILED);
    fail_begin=false; assert(update_engine_begin(&u,256,4096,digest)); fail_write=true;
    assert(!update_engine_write(&u,0,chunk,256)); assert(aborted==1 && u.offset==0);
    fail_write=false; assert(update_engine_begin(&u,256,4096,digest));
    assert(update_engine_write(&u,0,chunk,256)); fail_finish=true;
    assert(!update_engine_finish(&u)); assert(!update_engine_activate(&u));
    fail_finish=false; assert(update_engine_begin(&u,256,4096,digest));
    assert(update_engine_abort(&u)); assert(aborted==2 && u.state==UPDATE_IDLE);
    assert(update_engine_begin(&u,256,4096,digest)); assert(update_engine_write(&u,0,chunk,256));
    assert(update_engine_finish(&u)); assert(update_engine_abort(&u)); assert(aborted==2);
    assert(update_engine_begin(&u,256,4096,digest)); assert(update_engine_write(&u,0,chunk,256));
    assert(update_engine_finish(&u)); fail_activate=true;
    assert(!update_engine_activate(&u)); assert(u.state==UPDATE_BOOT_UNCERTAIN);
    assert(!update_engine_abort(&u)); assert(!update_engine_begin(&u,256,4096,digest));
    puts("update_engine: bounds, ordering, truncation, checksum/backend failures and activation barriers passed");
    return 0;
}
