#include "board_io.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "freertos/task.h"
#include "esp_log.h"

static QueueHandle_t frames;
static rmt_channel_handle_t channels[2];
static rmt_encoder_handle_t encoders[2];
static portMUX_TYPE fault_mux=portMUX_INITIALIZER_UNLOCKED;
static uint32_t fault_mask;
uint32_t board_lights_faults(void)
{
    portENTER_CRITICAL(&fault_mux); uint32_t value=fault_mask; portEXIT_CRITICAL(&fault_mux);
    return value;
}
/* Only light task owns these; never reused while hardware still transmits. */
static rmt_symbol_word_t symbols[2][BOARD_KEY_LED_COUNT * 24 + 1];

static void light_task(void *arg)
{
    (void)arg;
    board_light_frame_t frame={0};
    bool faulted[2]={false,false};
    const int pins[] = {BOARD_STATUS1, BOARD_STATUS2, BOARD_STATUS3};
    for (;;) {
        bool fresh=xQueueReceive(frames, &frame, pdMS_TO_TICKS(100))==pdTRUE;
        unsigned brightness = frame.brightness > 64 ? 64 : frame.brightness;
        if (!fresh) continue;
        for (unsigned chain=0; chain<2; ++chain) {
            if (faulted[chain]) continue; /* Retain outstanding payload forever; reboot recovers. */
            unsigned count = chain ? BOARD_AMBIENT_LED_COUNT : BOARD_KEY_LED_COUNT;
            board_rgb_t *rgb = chain ? frame.ambient : frame.keys;
            unsigned n=0;
            for (unsigned i=0; i<count; ++i) {
                uint8_t grb[] = {rgb[i].g * brightness / 255,
                                 rgb[i].r * brightness / 255, rgb[i].b * brightness / 255};
                for (unsigned c=0; c<3; ++c) for (int bit=7; bit>=0; --bit) {
                    bool one = (grb[c] >> bit) & 1;
                    /* 10MHz clock: SK6812 0=0.3/0.9us, 1=0.6/0.6us;
                     * WS2812 V6 0=0.3/0.9us, 1=0.9/0.3us. */
                    unsigned high = one ? (chain ? 9 : 6) : 3;
                    symbols[chain][n++] = (rmt_symbol_word_t){.level0=1,
                        .duration0=high, .level1=0, .duration1=12-high};
                }
            }
            symbols[chain][n++] = (rmt_symbol_word_t){.duration0=1600, .duration1=1600};
            rmt_transmit_config_t cfg = {0};
            esp_err_t err = rmt_transmit(channels[chain], encoders[chain],
                                         symbols[chain], n*sizeof(rmt_symbol_word_t), &cfg);
            if (err == ESP_OK) {
                err = rmt_tx_wait_all_done(channels[chain], 100);
            }
            if (err != ESP_OK) {
                faulted[chain]=true;
                portENTER_CRITICAL(&fault_mux); fault_mask |= 1U<<chain; portEXIT_CRITICAL(&fault_mux);
                ESP_LOGE("lights", "RMT chain %u stopped: %s", chain, esp_err_to_name(err));
            }
        }
        for (unsigned i=0; i<3; ++i) gpio_set_level(pins[i], (frame.status_mask >> i) & 1);
    }
}

esp_err_t board_lights_submit(const board_light_frame_t *frame)
{
    if (!frame || !frames) return ESP_ERR_INVALID_STATE;
    return xQueueOverwrite(frames, frame) == pdPASS ? ESP_OK : ESP_FAIL;
}

esp_err_t board_lights_start(void)
{
    if (frames) return ESP_ERR_INVALID_STATE;
    const int pins[] = {BOARD_KEYS_RGB, BOARD_AMBIENT_RGB};
    gpio_config_t status = {.pin_bit_mask=(1ULL<<BOARD_STATUS1)|(1ULL<<BOARD_STATUS2)|
        (1ULL<<BOARD_STATUS3), .mode=GPIO_MODE_OUTPUT};
    ESP_ERROR_CHECK(gpio_config(&status));
    for (unsigned i=0; i<2; ++i) {
        rmt_tx_channel_config_t cfg = {.gpio_num=pins[i], .clk_src=RMT_CLK_SRC_DEFAULT,
            .resolution_hz=10000000, .mem_block_symbols=64, .trans_queue_depth=1};
        ESP_ERROR_CHECK(rmt_new_tx_channel(&cfg, &channels[i]));
        rmt_copy_encoder_config_t enc = {};
        ESP_ERROR_CHECK(rmt_new_copy_encoder(&enc, &encoders[i]));
        ESP_ERROR_CHECK(rmt_enable(channels[i]));
    }
    frames = xQueueCreate(1, sizeof(board_light_frame_t));
    if (!frames) return ESP_ERR_NO_MEM;
    if (xTaskCreate(light_task, "board_lights", 3072, NULL, 2, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    board_light_frame_t off = {0};
    return board_lights_submit(&off);
}
