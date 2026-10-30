#include "usb_microphone.h"
#include "sdkconfig.h"
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "board_definition.h"
#include "microphone_pcm.h"
#include "tusb.h"

#ifdef CONFIG_CODEX_USB_MICROPHONE
/* I2S clock drives the single FIFO producer, as recommended by TinyUSB.
 * USB ISR is the consumer and only updates bounded diagnostic counters. */
enum { AUDIO_CONTROL_ITF=4, AUDIO_STREAM_ITF=5, BLOCK_FRAMES=96 };
static portMUX_TYPE mic_mux=portMUX_INITIALIZER_UNLOCKED;
static uint32_t generation;
static bool upgrade_blocked;
static usb_microphone_status_t state;
static i2s_chan_handle_t channel;
static uint8_t mute[2];
static int16_t volume[2]; /* UAC units: 1/256 dB, -24..+24 dB. */
static int32_t gain_q16=65536;

bool usb_microphone_upgrade_hold(void)
{
    portENTER_CRITICAL(&mic_mux);
    bool ok=!state.streaming;
    if (ok) upgrade_blocked=true;
    portEXIT_CRITICAL(&mic_mux);
    return ok;
}
void usb_microphone_upgrade_release(void)
{
    portENTER_CRITICAL(&mic_mux); upgrade_blocked=false; portEXIT_CRITICAL(&mic_mux);
}
static bool stream_set(bool active)
{
    portENTER_CRITICAL(&mic_mux);
    if (active && upgrade_blocked) { portEXIT_CRITICAL(&mic_mux); return false; }
    state.streaming=active && state.initialized;
    ++generation;
    portEXIT_CRITICAL(&mic_mux);
    return true;
}
void usb_microphone_link_down(void) { stream_set(false); }
void usb_microphone_status(usb_microphone_status_t *out)
{
    portENTER_CRITICAL(&mic_mux); *out=state; portEXIT_CRITICAL(&mic_mux);
}
static void capture(void *arg)
{
    (void)arg;
    int32_t raw[BLOCK_FRAMES*2];
    int16_t pcm[BLOCK_FRAMES];
    for (;;) {
        uint32_t epoch; bool active; int32_t gain;
        portENTER_CRITICAL(&mic_mux);
        epoch=generation; active=state.streaming; gain=gain_q16;
        portEXIT_CRITICAL(&mic_mux);
        size_t bytes=0;
        esp_err_t err=i2s_channel_read(channel,raw,sizeof(raw),&bytes,100);
        if (err!=ESP_OK || bytes%8) {
            portENTER_CRITICAL(&mic_mux); ++state.read_errors; portEXIT_CRITICAL(&mic_mux);
            vTaskDelay(pdMS_TO_TICKS(5)); continue;
        }
        unsigned frames=bytes/8,peak=0;
        for (unsigned i=0;i<frames;++i) {
            /* Stereo clock slots always generate 64 BCLK/WS. L/R=GND: left. */
            pcm[i]=microphone_pcm16(raw[2*i],gain);
            unsigned magnitude=pcm[i]<0?-(int32_t)pcm[i]:pcm[i];
            if (magnitude>peak) peak=magnitude;
        }
        portENTER_CRITICAL(&mic_mux);
        state.frames+=frames; state.peak=peak;
        if (active && state.streaming && epoch==generation) {
            tu_fifo_t *fifo=tud_audio_n_get_ep_in_ff(0);
            if (fifo) {
                unsigned space=tu_fifo_remaining(fifo)/2;
                unsigned n=frames<space?frames:space;
                unsigned written=tud_audio_write(pcm,n*2)/2;
                state.dropped+=frames-written;
                state.fifo_bytes=tu_fifo_count(fifo);
            }
        }
        portEXIT_CRITICAL(&mic_mux);
    }
}
esp_err_t usb_microphone_start(void)
{
    i2s_chan_config_t cfg=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,I2S_ROLE_MASTER);
    cfg.dma_desc_num=6; cfg.dma_frame_num=BLOCK_FRAMES;
    esp_err_t err=i2s_new_channel(&cfg,NULL,&channel);
    if (err!=ESP_OK) goto failed;
    i2s_std_config_t std={
        .clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(48000),
        .slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,I2S_SLOT_MODE_STEREO),
        .gpio_cfg={.mclk=I2S_GPIO_UNUSED,.bclk=BOARD_MIC_BCLK,.ws=BOARD_MIC_WS,
            .dout=I2S_GPIO_UNUSED,.din=BOARD_MIC_DIN}
    };
    err=i2s_channel_init_std_mode(channel,&std);
    if (err!=ESP_OK) goto failed;
    err=i2s_channel_enable(channel);
    if (err!=ESP_OK) goto failed;
    /* Publish before USB install; the host cannot open the stream yet. */
    state.initialized=true;
    if (xTaskCreate(capture,"mic_capture",4096,NULL,5,NULL)!=pdPASS) {
        state.initialized=false; err=ESP_ERR_NO_MEM;
        i2s_channel_disable(channel); goto failed;
    }
    return ESP_OK;
failed:
    if (channel) { i2s_del_channel(channel); channel=NULL; }
    state.error=err;
    return err;
}

bool tud_audio_set_itf_cb(uint8_t rhport,tusb_control_request_t const *req)
{
    (void)rhport;
    if (req->wIndex!=AUDIO_STREAM_ITF || req->wValue>1) return false;
    return stream_set(req->wValue==1);
}
bool tud_audio_set_itf_close_ep_cb(uint8_t rhport,tusb_control_request_t const *req)
{
    (void)rhport; (void)req; stream_set(false); return true;
}
bool tud_audio_tx_done_isr(uint8_t rhport,uint16_t bytes,uint8_t func,uint8_t ep,uint8_t alt)
{
    (void)rhport;(void)func;(void)ep;(void)alt;
    portENTER_CRITICAL_ISR(&mic_mux);
    if (state.streaming) {
        ++state.usb_packets;
        state.usb_bytes+=bytes;
        if (!bytes) ++state.underruns;
    }
    portEXIT_CRITICAL_ISR(&mic_mux);
    return true;
}
static bool control_target(tusb_control_request_t const *req)
{ return (req->wIndex&255)==AUDIO_CONTROL_ITF && (req->wValue&255)<=1; }
static void gain_update(void)
{
    int32_t gain=(mute[0] || mute[1])?0:(int32_t)lroundf(65536.0f*powf(10.0f,(volume[0]+volume[1])/5120.0f));
    portENTER_CRITICAL(&mic_mux); gain_q16=gain; portEXIT_CRITICAL(&mic_mux);
}
bool tud_audio_set_req_entity_cb(uint8_t rhport,tusb_control_request_t const *req,uint8_t *buf)
{
    (void)rhport;
    if (!control_target(req) || (req->wIndex>>8)!=2 || req->bRequest!=AUDIO20_CS_REQ_CUR) return false;
    unsigned ch=req->wValue&255,sel=req->wValue>>8;
    if (sel==AUDIO20_FU_CTRL_MUTE && req->wLength==1 && buf[0]<=1) mute[ch]=buf[0];
    else if (sel==AUDIO20_FU_CTRL_VOLUME && req->wLength==2) {
        int16_t value; memcpy(&value,buf,2);
        if (value< -24*256 || value>24*256 || value%256) return false;
        volume[ch]=value;
    } else return false;
    gain_update(); return true;
}
bool tud_audio_get_req_entity_cb(uint8_t rhport,tusb_control_request_t const *req)
{
    if (!control_target(req)) return false;
    unsigned entity=req->wIndex>>8,sel=req->wValue>>8,ch=req->wValue&255;
    if (entity==4 && ch==0 && sel==AUDIO20_CS_CTRL_SAM_FREQ) {
        if (req->bRequest==AUDIO20_CS_REQ_CUR) {
            uint32_t rate=48000;
            return tud_audio_buffer_and_schedule_control_xfer(rhport,req,&rate,sizeof(rate));
        }
        if (req->bRequest==AUDIO20_CS_REQ_RANGE) {
            audio20_control_range_4_n_t(1) range={.wNumSubRanges=1,.subrange={{48000,48000,0}}};
            return tud_audio_buffer_and_schedule_control_xfer(rhport,req,&range,sizeof(range));
        }
    }
    if (entity==4 && ch==0 && sel==AUDIO20_CS_CTRL_CLK_VALID && req->bRequest==AUDIO20_CS_REQ_CUR) {
        uint8_t valid=state.initialized;
        return tud_audio_buffer_and_schedule_control_xfer(rhport,req,&valid,1);
    }
    if (entity==1 && sel==AUDIO20_TE_CTRL_CONNECTOR && req->bRequest==AUDIO20_CS_REQ_CUR) {
        audio20_desc_channel_cluster_t cluster={.bNrChannels=1,.bmChannelConfig=0,.iChannelNames=0};
        return tud_audio_buffer_and_schedule_control_xfer(rhport,req,&cluster,sizeof(cluster));
    }
    if (entity==2 && sel==AUDIO20_FU_CTRL_MUTE && req->bRequest==AUDIO20_CS_REQ_CUR)
        return tud_audio_buffer_and_schedule_control_xfer(rhport,req,&mute[ch],1);
    if (entity==2 && sel==AUDIO20_FU_CTRL_VOLUME) {
        if (req->bRequest==AUDIO20_CS_REQ_CUR)
            return tud_audio_buffer_and_schedule_control_xfer(rhport,req,&volume[ch],2);
        if (req->bRequest==AUDIO20_CS_REQ_RANGE) {
            audio20_control_range_2_n_t(1) range={.wNumSubRanges=1,.subrange={{-24*256,24*256,256}}};
            return tud_audio_buffer_and_schedule_control_xfer(rhport,req,&range,sizeof(range));
        }
    }
    return false;
}
#else
esp_err_t usb_microphone_start(void) { return ESP_ERR_NOT_SUPPORTED; }
void usb_microphone_link_down(void) {}
void usb_microphone_status(usb_microphone_status_t *out) { memset(out,0,sizeof(*out)); }
bool usb_microphone_upgrade_hold(void) { return true; }
void usb_microphone_upgrade_release(void) {}
#endif
