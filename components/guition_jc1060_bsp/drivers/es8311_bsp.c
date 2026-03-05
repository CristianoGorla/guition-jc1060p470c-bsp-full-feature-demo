#include "es8311_bsp.h"
#include "esp_log.h"
#include "esp_check.h"
#include "bsp_log_panel.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = BSP_LOG_TAG;

#define LOG_UNIT "AUDIO"
#define LOGI(fmt, ...) BSP_LOGI_PANEL(LOG_UNIT, fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) BSP_LOGW_PANEL(LOG_UNIT, fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) BSP_LOGE_PANEL(LOG_UNIT, fmt, ##__VA_ARGS__)

/* Hardware Pin Configuration */
#define ES8311_I2C_ADDRESS     0x18
#define NS4150_PA_CTRL_GPIO    GPIO_NUM_11  /* Power Amplifier Enable */

/* I2S Pin Configuration */
#define I2S_MCLK_GPIO          GPIO_NUM_9
#define I2S_BCLK_GPIO          GPIO_NUM_13  /* I2S_SCLK */
#define I2S_WS_GPIO            GPIO_NUM_12  /* I2S_LRCK */
#define I2S_DOUT_GPIO          GPIO_NUM_10  /* I2S_DSDIN - to codec */

/* External I2C handle (initialized by bsp_i2c_init) */
extern i2c_master_bus_handle_t g_i2c_bus_handle;

static i2s_chan_handle_t g_i2s_tx_handle = NULL;

/**
 * @brief Initialize NS4150 power amplifier control pin
 */
static esp_err_t ns4150_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << NS4150_PA_CTRL_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "Failed to configure PA control GPIO");
    
    /* Start with PA disabled */
    gpio_set_level(NS4150_PA_CTRL_GPIO, 0);
    
    LOGI( "NS4150 amplifier control initialized (GPIO %d)", NS4150_PA_CTRL_GPIO);
    return ESP_OK;
}

/**
 * @brief Initialize I2S interface for audio output
 */
static esp_err_t i2s_init(uint32_t sample_rate, uint8_t bits_per_sample)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &g_i2s_tx_handle, NULL), TAG,
                        "Failed to create I2S TX channel");

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = sample_rate,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bits_per_sample == 16 ? I2S_DATA_BIT_WIDTH_16BIT : I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_MCLK_GPIO,
            .bclk = I2S_BCLK_GPIO,
            .ws = I2S_WS_GPIO,
            .dout = I2S_DOUT_GPIO,
            .din = GPIO_NUM_NC,  /* No input */
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(g_i2s_tx_handle, &std_cfg), TAG,
                        "Failed to initialize I2S standard mode");
    
    ESP_RETURN_ON_ERROR(i2s_channel_enable(g_i2s_tx_handle), TAG,
                        "Failed to enable I2S channel");

    LOGI( "I2S initialized (%lu Hz, %d-bit, MCLK=%d, BCLK=%d, WS=%d, DOUT=%d)",
             sample_rate, bits_per_sample, I2S_MCLK_GPIO, I2S_BCLK_GPIO, I2S_WS_GPIO, I2S_DOUT_GPIO);
    return ESP_OK;
}

/**
 * @brief Initialize ES8311 codec via I2C
 * 
 * Note: This is a simplified initialization. For production, you would
 * typically use a dedicated ES8311 driver library with full register configuration.
 */
static esp_err_t es8311_codec_init(void)
{
    if (g_i2c_bus_handle == NULL) {
        LOGE( "I2C bus not initialized! Call bsp_i2c_init() first");
        return ESP_ERR_INVALID_STATE;
    }

    /* Add ES8311 device to I2C bus */
    i2c_master_dev_handle_t dev_handle;
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ES8311_I2C_ADDRESS,
        .scl_speed_hz = 400000,
    };
    
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(g_i2c_bus_handle, &dev_cfg, &dev_handle), TAG,
                        "Failed to add ES8311 to I2C bus");

    /* TODO: Add ES8311 register initialization sequence here */
    /* For now, assume codec is in a workable default state */
    
    LOGI( "ES8311 codec initialized (I2C address 0x%02X)", ES8311_I2C_ADDRESS);
    return ESP_OK;
}

esp_err_t bsp_audio_init(const bsp_audio_config_t *config)
{
    LOGI( "Initializing ES8311 + NS4150 audio system");

    /* Use default config if none provided */
    bsp_audio_config_t default_cfg = BSP_AUDIO_DEFAULT_CONFIG();
    if (config == NULL) {
        config = &default_cfg;
    }

    /* Initialize NS4150 amplifier control */
    ESP_RETURN_ON_ERROR(ns4150_init(), TAG, "Failed to initialize NS4150");

    /* Initialize I2S interface */
    ESP_RETURN_ON_ERROR(i2s_init(config->sample_rate, config->bits_per_sample), TAG,
                        "Failed to initialize I2S");

    /* Initialize ES8311 codec */
    ESP_RETURN_ON_ERROR(es8311_codec_init(), TAG, "Failed to initialize ES8311");

    /* Enable power amplifier if requested */
    if (config->enable_pa) {
        ESP_RETURN_ON_ERROR(bsp_audio_set_pa_enable(true), TAG, "Failed to enable PA");
    }

    LOGI( "Audio system initialized (%lu Hz, %d-bit, PA %s)",
             config->sample_rate, config->bits_per_sample, config->enable_pa ? "enabled" : "disabled");
    return ESP_OK;
}

esp_err_t bsp_audio_set_pa_enable(bool enable)
{
    gpio_set_level(NS4150_PA_CTRL_GPIO, enable ? 1 : 0);
    LOGI( "NS4150 power amplifier %s", enable ? "enabled" : "disabled");
    return ESP_OK;
}

esp_err_t bsp_audio_set_volume(uint8_t volume)
{
    if (volume > 100) {
        volume = 100;
    }
    
    /* TODO: Implement ES8311 volume control via I2C registers */
    LOGW( "Volume control not yet implemented (requested: %d%%)", volume);
    
    return ESP_OK;
}
