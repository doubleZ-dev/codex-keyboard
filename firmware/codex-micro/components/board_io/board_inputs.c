#include "board_io.h"
#include "matrix_debounce.h"
#include "joystick_filter.h"
#include "battery_monitor.h"
#include "driver/gpio.h"
#include "driver/touch_sens.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <stdlib.h>

static QueueHandle_t output;
static TaskHandle_t input_task;
static esp_timer_handle_t scan_timer;
static portMUX_TYPE snapshot_mux = portMUX_INITIALIZER_UNLOCKED;
static board_input_snapshot_t published;
static adc_oneshot_unit_handle_t adc;
static adc_cali_handle_t calibration;
static bool touch_ready;
static touch_sensor_handle_t touch_sensor;
static touch_channel_handle_t touch_channel;
static uint32_t sequence;
static SemaphoreHandle_t scan_lock;

void board_inputs_resync(board_input_snapshot_t *out)
{
    xSemaphoreTake(scan_lock,portMAX_DELAY);
    xQueueReset(output);
    board_inputs_snapshot(out);
    xSemaphoreGive(scan_lock);
}

void board_inputs_snapshot(board_input_snapshot_t *out)
{
    if (!out) return;
    portENTER_CRITICAL(&snapshot_mux);
    *out = published;
    portEXIT_CRITICAL(&snapshot_mux);
}

static void emit(input_event_t event, board_input_snapshot_t *state)
{
    event.sequence = ++sequence;
    if (xQueueSend(output, &event, 0) != pdTRUE) ++state->events_lost;
}

static void edge(uint16_t id, bool down, uint64_t now, board_input_snapshot_t *state)
{
    input_event_t event = {.physical_id=id, .time_us=now,
        .kind=id == BOARD_TOUCH_ID ? IN_TOUCH_EDGE : IN_KEY_EDGE};
    event.value.edge.down = down;
    emit(event, state);
}

static void timer_callback(void *arg)
{
    (void)arg;
    xTaskNotifyGive(input_task); /* esp_timer task context; no GPIO or ADC here */
}

static void scan_task(void *arg)
{
    (void)arg;
    board_input_snapshot_t state = {0};
    joystick_filter_t joystick; joystick_filter_init(&joystick);
    battery_monitor_t battery={0};
    matrix_debounce_t matrix, buttons;
    matrix_debounce_init(&matrix, 5000);
    matrix_debounce_init(&buttons, 5000); /* bits 0,1 are valid positions */
    int previous_ab = (gpio_get_level(BOARD_ENCODER_A)<<1)|gpio_get_level(BOARD_ENCODER_B);
    int steps=0;
    const int8_t quadrature[16] = {0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};
    uint64_t next_adc=0, next_battery=0, next_touch=0;
    uint32_t touch_baseline=0, touch_samples=0;
    unsigned touch_count=0;
    for (;;) {
        uint32_t ticks = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        xSemaphoreTake(scan_lock,portMAX_DELAY);
        if (ticks > 1) state.scan_overruns += ticks-1;
        uint64_t now = esp_timer_get_time();
        uint16_t raw=0, down=0, up=0;
        for (unsigned c=0; c<4; ++c) {
            gpio_set_level(board_matrix_cols[c], 0);
            gpio_set_direction(board_matrix_cols[c], GPIO_MODE_OUTPUT);
            esp_rom_delay_us(5);
            for (unsigned r=0; r<4; ++r)
                if (!gpio_get_level(board_matrix_rows[r])) raw |= 1U<<(r*4+c);
            gpio_set_direction(board_matrix_cols[c], GPIO_MODE_INPUT); /* Hi-Z */
        }
        matrix_debounce_sample(&matrix, raw, now, &down, &up);
        state.matrix_down = matrix.stable;
        for (unsigned i=0; i<16; ++i) {
            if (down & (1U<<i)) edge(i,true,now,&state);
            if (up & (1U<<i)) edge(i,false,now,&state);
        }
        raw = (!gpio_get_level(BOARD_ENCODER_SW)) | ((!gpio_get_level(BOARD_JOYSTICK_SW))<<1);
        matrix_debounce_sample(&buttons, raw, now, &down, &up);
        state.encoder_down = buttons.stable & 1;
        state.joystick_down = (buttons.stable & 2) != 0;
        const uint16_t ids[] = {BOARD_ENCODER_SW_ID, BOARD_JOYSTICK_SW_ID};
        for (unsigned i=0; i<2; ++i) {
            if (down & (1U<<i)) edge(ids[i],true,now,&state);
            if (up & (1U<<i)) edge(ids[i],false,now,&state);
        }
        int ab=(gpio_get_level(BOARD_ENCODER_A)<<1)|gpio_get_level(BOARD_ENCODER_B);
        if ((previous_ab ^ ab) == 3) steps=0; /* invalid diagonal: never invent motion */
        else steps += quadrature[previous_ab*4+ab];
        previous_ab=ab;
        if (abs(steps) >= 4) {
            input_event_t e = {.time_us=now,.physical_id=BOARD_ENCODER_ROT_ID,.kind=IN_ENCODER_DELTA};
            e.value.encoder.detents=steps>0?1:-1;
            emit(e,&state); steps=0;
        }
        if (now >= next_adc) {
            next_adc=now+5000;
            int x,y;
            if (adc_oneshot_read(adc, BOARD_JOYSTICK_X_ADC_CHANNEL,&x)==ESP_OK &&
                adc_oneshot_read(adc, BOARD_JOYSTICK_Y_ADC_CHANNEL,&y)==ESP_OK) {
                int16_t nx,ny;
                joystick_filter_update(&joystick,x,y,&nx,&ny);
                state.joystick_raw_x=x; state.joystick_raw_y=y;
                state.joystick_center_x=joystick.center[0]; state.joystick_center_y=joystick.center[1];
                state.joystick_calibrated=joystick.ready;
                if (abs(nx-state.joystick_x)>1024 || abs(ny-state.joystick_y)>1024 ||
                    (!nx && state.joystick_x) || (!ny && state.joystick_y)) {
                    state.joystick_x=nx; state.joystick_y=ny;
                    input_event_t e={.time_us=now,.physical_id=BOARD_JOYSTICK_VECTOR_ID,.kind=IN_JOYSTICK_VECTOR};
                    e.value.joystick.x_q15=nx; e.value.joystick.y_q15=ny; emit(e,&state);
                }
            } else ++state.adc_errors;
        }
        if (now >= next_battery) {
            next_battery=now+BATTERY_SAMPLE_US;
            int raw_battery=0, mv=0;
            bool valid=calibration && adc_oneshot_read(adc,BOARD_BATTERY_ADC_CHANNEL,&raw_battery)==ESP_OK &&
                adc_cali_raw_to_voltage(calibration,raw_battery,&mv)==ESP_OK;
            if (!valid) ++state.adc_errors;
            state.battery_adc_raw=raw_battery; state.battery_adc_mv=mv;
            battery_monitor_sample(&battery,valid,mv);
            state.battery_mv=battery.millivolts;
            state.battery_valid=battery.valid; state.low_voltage=battery.low;
        }
        if (touch_ready && now>=next_touch) {
            next_touch=now+10000;
            uint32_t value;
            if (touch_channel_read_data(touch_channel,TOUCH_CHAN_DATA_TYPE_RAW,&value)==ESP_OK && value) {
                if (touch_samples<32) { touch_baseline += value; if (++touch_samples==32) touch_baseline/=32; }
                else {
                    bool active=value>touch_baseline+touch_baseline*(state.touch_down?8:12)/100;
                    if (active!=state.touch_down) {
                        if (++touch_count>=3) { state.touch_down=active; edge(BOARD_TOUCH_ID,active,now,&state); touch_count=0; }
                    } else touch_count=0;
                    if (!active && !state.touch_down) touch_baseline=(touch_baseline*63ULL+value)/64;
                }
            } else ++state.touch_errors;
        }
        portENTER_CRITICAL(&snapshot_mux); published=state; portEXIT_CRITICAL(&snapshot_mux);
        xSemaphoreGive(scan_lock);
    }
}

esp_err_t board_inputs_start(QueueHandle_t events)
{
    if (!events || output) return ESP_ERR_INVALID_STATE;
    if (!board_definition_valid()) return ESP_ERR_INVALID_ARG;
    output=events;
    scan_lock=xSemaphoreCreateMutex();
    if (!scan_lock) return ESP_ERR_NO_MEM;
    uint64_t mask=(1ULL<<BOARD_ENCODER_A)|(1ULL<<BOARD_ENCODER_B)|
                  (1ULL<<BOARD_ENCODER_SW)|(1ULL<<BOARD_JOYSTICK_SW);
    for (unsigned i=0;i<4;++i) mask |= 1ULL<<board_matrix_rows[i];
    gpio_config_t pins={.pin_bit_mask=mask,.mode=GPIO_MODE_INPUT,.pull_up_en=GPIO_PULLUP_ENABLE};
    ESP_ERROR_CHECK(gpio_config(&pins));
    mask=0; for (unsigned i=0;i<4;++i) mask |= 1ULL<<board_matrix_cols[i];
    pins=(gpio_config_t){.pin_bit_mask=mask,.mode=GPIO_MODE_INPUT};
    ESP_ERROR_CHECK(gpio_config(&pins));
    adc_oneshot_unit_init_cfg_t unit={.unit_id=ADC_UNIT_1};
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit,&adc));
    adc_oneshot_chan_cfg_t channel={.atten=ADC_ATTEN_DB_12,.bitwidth=ADC_BITWIDTH_12};
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc,BOARD_BATTERY_ADC_CHANNEL,&channel));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc,BOARD_JOYSTICK_X_ADC_CHANNEL,&channel));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc,BOARD_JOYSTICK_Y_ADC_CHANNEL,&channel));
    adc_cali_curve_fitting_config_t cal={.unit_id=ADC_UNIT_1,.chan=BOARD_BATTERY_ADC_CHANNEL,
        .atten=ADC_ATTEN_DB_12,.bitwidth=ADC_BITWIDTH_12};
    esp_err_t err=adc_cali_create_scheme_curve_fitting(&cal,&calibration);
    if (err!=ESP_OK) ESP_LOGW("inputs","Battery calibrated voltage unavailable: %s",esp_err_to_name(err));
    touch_sensor_sample_config_t sample = TOUCH_SENSOR_V2_DEFAULT_SAMPLE_CONFIG(
        500, TOUCH_VOLT_LIM_L_0V5, TOUCH_VOLT_LIM_H_2V2);
    touch_sensor_config_t sensor_cfg = TOUCH_SENSOR_DEFAULT_BASIC_CONFIG(1, &sample);
    touch_channel_config_t touch_cfg = {.active_thresh={2000},
        .charge_speed=TOUCH_CHARGE_SPEED_7,.init_charge_volt=TOUCH_INIT_CHARGE_VOLT_DEFAULT};
    err=touch_sensor_new_controller(&sensor_cfg,&touch_sensor);
    if (err==ESP_OK) err=touch_sensor_new_channel(touch_sensor,BOARD_TOUCH_CHANNEL,&touch_cfg,&touch_channel);
    if (err==ESP_OK) err=touch_sensor_enable(touch_sensor);
    if (err==ESP_OK) err=touch_sensor_start_continuous_scanning(touch_sensor);
    touch_ready=err==ESP_OK;
    if (!touch_ready) ESP_LOGE("inputs","Touch unavailable: %s",esp_err_to_name(err));
    if (xTaskCreate(scan_task,"board_inputs",4096,NULL,5,&input_task)!=pdPASS) return ESP_ERR_NO_MEM;
    esp_timer_create_args_t timer={.callback=timer_callback,.name="input_scan",.skip_unhandled_events=true};
    ESP_ERROR_CHECK(esp_timer_create(&timer,&scan_timer));
    return esp_timer_start_periodic(scan_timer,1000);
}
