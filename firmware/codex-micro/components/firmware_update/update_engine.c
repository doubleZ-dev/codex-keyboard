#include "update_engine.h"
#include <string.h>
void update_engine_init(update_engine_t *u, update_backend_t backend)
{ memset(u,0,sizeof(*u)); u->backend=backend; }
bool update_engine_begin(update_engine_t *u,uint32_t size,uint32_t capacity,const uint8_t digest[32])
{
    if ((u->state!=UPDATE_IDLE && u->state!=UPDATE_FAILED) || size<256 || size>capacity || !digest) return false;
    u->offset=0; u->size=size; memcpy(u->digest,digest,32);
    if (!u->backend.begin(size,u->backend.ctx)) { u->state=UPDATE_FAILED; return false; }
    u->state=UPDATE_WRITING; return true;
}
bool update_engine_write(update_engine_t *u,uint32_t offset,const uint8_t *data,size_t length)
{
    if (u->state!=UPDATE_WRITING || offset!=u->offset || !data || !length ||
        length>UPDATE_CHUNK_MAX || length>u->size-u->offset) return false;
    if (!u->backend.write(data,length,u->backend.ctx)) {
        u->backend.abort(u->backend.ctx); u->state=UPDATE_FAILED; return false;
    }
    u->offset+=(uint32_t)length; return true;
}
bool update_engine_finish(update_engine_t *u)
{
    if (u->state!=UPDATE_WRITING || u->offset!=u->size) return false;
    bool valid=u->backend.finish(u->digest,u->backend.ctx);
    u->state=valid?UPDATE_READY:UPDATE_FAILED; return valid;
}
bool update_engine_activate(update_engine_t *u)
{
    if (u->state!=UPDATE_READY) return false;
    /* A failed redundant otadata write may already have changed one copy.
     * Do not allow this slot to be overwritten until the next boot resolves it. */
    if (!u->backend.activate(u->backend.ctx)) { u->state=UPDATE_BOOT_UNCERTAIN; return false; }
    u->state=UPDATE_ACTIVATED; return true;
}
bool update_engine_abort(update_engine_t *u)
{
    if (u->state==UPDATE_ACTIVATED || u->state==UPDATE_BOOT_UNCERTAIN) return false;
    if (u->state==UPDATE_WRITING) u->backend.abort(u->backend.ctx);
    u->state=UPDATE_IDLE; u->offset=u->size=0; return true;
}
