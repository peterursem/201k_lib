#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "lcos_201k.h"

static const char *TAG = "lcos_201k";

// Register definitions (from lcos_registers.c)
#define LCOS_REG_DISP_CFG       0x00
#define LCOS_REG_VID_CFG0       0x01
#define LCOS_REG_VID_CFG1       0x02
#define LCOS_REG_BRIGHTNESS     0x03
#define LCOS_REG_V_OFFSET       0x04
#define LCOS_REG_H_DELAY        0x05
#define LCOS_REG_VID_CFG3       0x06
#define LCOS_REG_SCALE_HI       0x07
#define LCOS_REG_H_SCALE        0x08
#define LCOS_REG_H_CYCLE        0x09
#define LCOS_REG_V_SCALE        0x0A
#define LCOS_REG_V_CYCLE        0x0B
#define LCOS_REG_V_DELAY        0x0C
#define LCOS_REG_CINEMA         0x0D
#define LCOS_REG_GAMMA          0x0E
#define LCOS_REG_HW_CFG         0x18

#define LCOS_VID_CFG3_VFLIP     (1 << 6)
#define LCOS_VID_CFG3_HFLIP     (1 << 5)
#define LCOS_VID_CFG3_NSLEEP    (1 << 3)

#define LCOS_CMD_WRITE          0x00
#define LCOS_CMD_READ           0x80
#define LCOS_HW_ID_EXPECTED     0x54

// Display resolution constants
#define DISP_H_RES              300
#define DISP_V_RES              224
#define PCLK_FREQ_HZ            26666666  // 160MHz/6 for clean division

// 60Hz timing (default, more blanking margin)
#define TIMING_60HZ_H_TOTAL     1716
#define TIMING_60HZ_H_SYNC      10
#define TIMING_60HZ_H_BP        20
#define TIMING_60HZ_H_FP        (TIMING_60HZ_H_TOTAL - (DISP_H_RES * 3) - TIMING_60HZ_H_SYNC - TIMING_60HZ_H_BP)
#define TIMING_60HZ_V_TOTAL     262
#define TIMING_60HZ_V_SYNC      3
#define TIMING_60HZ_V_BP        18
#define TIMING_60HZ_V_FP        (TIMING_60HZ_V_TOTAL - DISP_V_RES - TIMING_60HZ_V_SYNC - TIMING_60HZ_V_BP)
#define TIMING_60HZ_H_DELAY     0x1C    // tHW + tHBP - 2 = 10 + 20 - 2 = 28
#define TIMING_60HZ_V_DELAY     0x15    // tVW + tVBP = 3 + 18 = 21

// High refresh timing: ~62Hz (max achievable)
// Tested range: V_TOTAL 254-330 works, H_TOTAL 1690-1716 works
#define TIMING_120HZ_H_TOTAL    1690    // Min working
#define TIMING_120HZ_H_SYNC     10
#define TIMING_120HZ_H_BP       20
#define TIMING_120HZ_H_FP       760
#define TIMING_120HZ_V_TOTAL    254     // Min working
#define TIMING_120HZ_V_SYNC     3
#define TIMING_120HZ_V_BP       18
#define TIMING_120HZ_V_FP       9
#define TIMING_120HZ_H_DELAY    0x1C
#define TIMING_120HZ_V_DELAY    0x15

// External register table
typedef struct {
    uint8_t addr;
    uint8_t value;
    const char *name;
} lcos_reg_entry_t;

extern const lcos_reg_entry_t lcos_default_regs[];
extern const size_t lcos_default_regs_count;

// Device structure
struct lcos_dev_s {
    lcos_config_t config;
    spi_device_handle_t spi;
    esp_lcd_panel_handle_t panel;

    // Framebuffer management
    void *framebuffer[2];           // [0] = front/only, [1] = back (if double buffer)
    uint8_t back_buffer_idx;        // Which buffer is for drawing (0 or 1)
    bool double_buffer;             // Cached from config

    // Vsync synchronization
    SemaphoreHandle_t vsync_sem;
    lcos_vsync_cb_t vsync_cb;
    void *vsync_user_data;
    volatile uint32_t frame_count;

    bool initialized;
    bool awake;
    uint8_t hw_revision;
};

// --- SPI Functions ---

static void spi_write_reg(spi_device_handle_t spi, uint8_t addr, uint8_t val) {
    spi_transaction_t t = {
        .flags = SPI_TRANS_USE_TXDATA,
        .length = 16,
        .tx_data = {(addr & 0x7F) | LCOS_CMD_WRITE, val}
    };
    ESP_ERROR_CHECK(spi_device_transmit(spi, &t));
}

static uint8_t spi_read_reg(spi_device_handle_t spi, uint8_t addr) {
    spi_transaction_t t = {
        .flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA,
        .length = 16,
        .tx_data = {(addr & 0x7F) | LCOS_CMD_READ, 0x00}
    };
    ESP_ERROR_CHECK(spi_device_transmit(spi, &t));
    return t.rx_data[1];
}

static esp_err_t init_spi(struct lcos_dev_s *dev) {
    const lcos_pin_config_t *pins = &dev->config.pins;

    spi_bus_config_t buscfg = {
        .miso_io_num = pins->spi_miso_gpio,
        .mosi_io_num = pins->spi_mosi_gpio,
        .sclk_io_num = pins->spi_clk_gpio,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32,
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 400000,  // 400 kHz
        .mode = 0,
        .spics_io_num = pins->spi_cs_gpio,
        .queue_size = 7,
    };

    esp_err_t ret = spi_bus_initialize(pins->spi_host, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = spi_bus_add_device(pins->spi_host, &devcfg, &dev->spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(ret));
        spi_bus_free(pins->spi_host);
        return ret;
    }

    return ESP_OK;
}

// --- Vsync Handler ---

static bool IRAM_ATTR vsync_handler(esp_lcd_panel_handle_t panel,
                                     const esp_lcd_rgb_panel_event_data_t *data,
                                     void *user_ctx) {
    struct lcos_dev_s *dev = user_ctx;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    dev->frame_count++;

    // Signal waiting task
    if (dev->vsync_sem) {
        xSemaphoreGiveFromISR(dev->vsync_sem, &xHigherPriorityTaskWoken);
    }

    // User callback
    if (dev->vsync_cb) {
        dev->vsync_cb(dev->vsync_user_data);
    }

    return xHigherPriorityTaskWoken == pdTRUE;
}

// --- LCD Functions ---

static esp_err_t init_lcd(struct lcos_dev_s *dev) {
    const lcos_pin_config_t *pins = &dev->config.pins;
    bool use_double = dev->config.display.double_buffer;
    bool use_120hz = (dev->config.display.refresh_rate == LCOS_REFRESH_HIGH);

    // Select timing based on refresh rate
    int h_sync, h_bp, h_fp, v_sync, v_bp, v_fp;
    if (use_120hz) {
        h_sync = TIMING_120HZ_H_SYNC;
        h_bp = TIMING_120HZ_H_BP;
        h_fp = TIMING_120HZ_H_FP;
        v_sync = TIMING_120HZ_V_SYNC;
        v_bp = TIMING_120HZ_V_BP;
        v_fp = TIMING_120HZ_V_FP;
        ESP_LOGI(TAG, "Using 120Hz timing (H_total=%d, V_total=%d)",
                 TIMING_120HZ_H_TOTAL, TIMING_120HZ_V_TOTAL);
    } else {
        h_sync = TIMING_60HZ_H_SYNC;
        h_bp = TIMING_60HZ_H_BP;
        h_fp = TIMING_60HZ_H_FP;
        v_sync = TIMING_60HZ_V_SYNC;
        v_bp = TIMING_60HZ_V_BP;
        v_fp = TIMING_60HZ_V_FP;
        ESP_LOGI(TAG, "Using 60Hz timing (H_total=%d, V_total=%d)",
                 TIMING_60HZ_H_TOTAL, TIMING_60HZ_V_TOTAL);
    }

    esp_lcd_rgb_panel_config_t panel_config = {
        .data_width = 8,
        .dma_burst_size = 64,
        .in_color_format = LCD_COLOR_FMT_RGB888,
        .out_color_format = LCD_COLOR_FMT_RGB888,
        .num_fbs = use_double ? 2 : 1,
        .bounce_buffer_size_px = use_double ? (DISP_H_RES * 8) : 0,  // 8 lines, must divide 201600
        .clk_src = LCD_CLK_SRC_PLL160M,
        .disp_gpio_num = -1,
        .pclk_gpio_num = pins->pclk_gpio,
        .vsync_gpio_num = pins->vsync_gpio,
        .hsync_gpio_num = pins->hsync_gpio,
        .de_gpio_num = pins->de_gpio,  // Internal VALID generation by default
        .data_gpio_nums = {
            pins->data_gpio[0], pins->data_gpio[1], pins->data_gpio[2], pins->data_gpio[3],
            pins->data_gpio[4], pins->data_gpio[5], pins->data_gpio[6], pins->data_gpio[7],
        },
        .timings = {
            .pclk_hz = PCLK_FREQ_HZ,
            .h_res = DISP_H_RES,
            .v_res = DISP_V_RES,
            .hsync_back_porch = h_bp,
            .hsync_front_porch = h_fp,
            .hsync_pulse_width = h_sync,
            .vsync_back_porch = v_bp,
            .vsync_front_porch = v_fp,
            .vsync_pulse_width = v_sync,
            .flags.pclk_active_neg = 0,
        },
        .flags.fb_in_psram = use_double ? 1 : 0,
    };

    esp_err_t ret = esp_lcd_new_rgb_panel(&panel_config, &dev->panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LCD panel create failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register vsync callback
    esp_lcd_rgb_panel_event_callbacks_t cbs = {
        .on_vsync = vsync_handler,
    };
    esp_lcd_rgb_panel_register_event_callbacks(dev->panel, &cbs, dev);

    ret = esp_lcd_panel_reset(dev->panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LCD panel reset failed: %s", esp_err_to_name(ret));
        esp_lcd_panel_del(dev->panel);
        return ret;
    }

    ret = esp_lcd_panel_init(dev->panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LCD panel init failed: %s", esp_err_to_name(ret));
        esp_lcd_panel_del(dev->panel);
        return ret;
    }

    // Get framebuffer(s)
    if (use_double) {
        ret = esp_lcd_rgb_panel_get_frame_buffer(dev->panel, 2,
                                                  &dev->framebuffer[0],
                                                  &dev->framebuffer[1]);
        dev->back_buffer_idx = 1;  // Start drawing to buffer 1
        ESP_LOGI(TAG, "Double buffering enabled (PSRAM)");
    } else {
        ret = esp_lcd_rgb_panel_get_frame_buffer(dev->panel, 1, &dev->framebuffer[0]);
        dev->framebuffer[1] = NULL;
        dev->back_buffer_idx = 0;
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Get framebuffer failed: %s", esp_err_to_name(ret));
        esp_lcd_panel_del(dev->panel);
        return ret;
    }

    dev->double_buffer = use_double;
    return ESP_OK;
}

// --- Public API ---

esp_err_t lcos_get_default_config(lcos_config_t *config) {
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(config, 0, sizeof(lcos_config_t));

    // Default pins (from Kconfig, matching the final board pinout)
    config->pins.pclk_gpio = CONFIG_LCOS_DEFAULT_PCLK_GPIO;
    config->pins.vsync_gpio = CONFIG_LCOS_DEFAULT_VSYNC_GPIO;
    config->pins.hsync_gpio = CONFIG_LCOS_DEFAULT_HSYNC_GPIO;
    config->pins.de_gpio = CONFIG_LCOS_DEFAULT_DE_GPIO;  // Internal VALID by default
    config->pins.data_gpio[0] = CONFIG_LCOS_DEFAULT_DATA0_GPIO;
    config->pins.data_gpio[1] = CONFIG_LCOS_DEFAULT_DATA1_GPIO;
    config->pins.data_gpio[2] = CONFIG_LCOS_DEFAULT_DATA2_GPIO;
    config->pins.data_gpio[3] = CONFIG_LCOS_DEFAULT_DATA3_GPIO;
    config->pins.data_gpio[4] = CONFIG_LCOS_DEFAULT_DATA4_GPIO;
    config->pins.data_gpio[5] = CONFIG_LCOS_DEFAULT_DATA5_GPIO;
    config->pins.data_gpio[6] = CONFIG_LCOS_DEFAULT_DATA6_GPIO;
    config->pins.data_gpio[7] = CONFIG_LCOS_DEFAULT_DATA7_GPIO;
    config->pins.nreset_gpio = CONFIG_LCOS_DEFAULT_NRESET_GPIO;
    config->pins.vcc_ena_gpio = CONFIG_LCOS_DEFAULT_VCC_ENA_GPIO;
    config->pins.spi_cs_gpio = CONFIG_LCOS_DEFAULT_SPI_CS_GPIO;
    config->pins.spi_clk_gpio = CONFIG_LCOS_DEFAULT_SPI_CLK_GPIO;
    config->pins.spi_mosi_gpio = CONFIG_LCOS_DEFAULT_SPI_MOSI_GPIO;
    config->pins.spi_miso_gpio = CONFIG_LCOS_DEFAULT_SPI_MISO_GPIO;
    config->pins.spi_host = SPI2_HOST;


    // Default display settings
    config->display.brightness = 0;  // Min
    config->display.gamma = 9;        // 2.1
    config->display.h_flip = false;
    config->display.v_flip = false;
    config->display.refresh_rate = LCOS_REFRESH_60HZ;  // Default to 60Hz

    return ESP_OK;
}

esp_err_t lcos_init(const lcos_config_t *config, lcos_handle_t *handle) {
    if (config == NULL || handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing LCoS 201k display");

    // Allocate device structure
    struct lcos_dev_s *dev = calloc(1, sizeof(struct lcos_dev_s));
    if (dev == NULL) {
        return ESP_ERR_NO_MEM;
    }

    memcpy(&dev->config, config, sizeof(lcos_config_t));

    // Create vsync semaphore
    dev->vsync_sem = xSemaphoreCreateBinary();
    if (dev->vsync_sem == NULL) {
        ESP_LOGE(TAG, "Failed to create vsync semaphore");
        free(dev);
        return ESP_ERR_NO_MEM;
    }

    // 1. Hold NRESET low
    gpio_set_direction(config->pins.nreset_gpio, GPIO_MODE_OUTPUT);
    gpio_set_level(config->pins.nreset_gpio, 0);

    // 2. Enable VCC (if configured)
    if (config->pins.vcc_ena_gpio >= 0) {
        gpio_set_direction(config->pins.vcc_ena_gpio, GPIO_MODE_OUTPUT);
        gpio_set_level(config->pins.vcc_ena_gpio, 1);
        ESP_LOGI(TAG, "VCC enabled (GPIO %d)", config->pins.vcc_ena_gpio);
        vTaskDelay(pdMS_TO_TICKS(10));  // Let power stabilize
    }

    // 3. Start LCD (provides clock)
    ESP_LOGI(TAG, "Initializing RGB Panel...");
    esp_err_t ret = init_lcd(dev);
    if (ret != ESP_OK) {
        free(dev);
        return ret;
    }

    // 3. Wait, release reset, wait for tSDLY
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_LOGI(TAG, "Releasing Reset...");
    gpio_set_level(config->pins.nreset_gpio, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    // 4. Initialize SPI
    ret = init_spi(dev);
    if (ret != ESP_OK) {
        esp_lcd_panel_del(dev->panel);
        free(dev);
        return ret;
    }

    // 5. Configure registers
    ESP_LOGI(TAG, "Configuring registers...");
    for (size_t i = 0; i < lcos_default_regs_count; i++) {
        spi_write_reg(dev->spi, lcos_default_regs[i].addr, lcos_default_regs[i].value);
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    // 5b. Override timing registers based on refresh rate
    uint8_t h_delay, v_delay;
    if (config->display.refresh_rate == LCOS_REFRESH_HIGH) {
        h_delay = TIMING_120HZ_H_DELAY;
        v_delay = TIMING_120HZ_V_DELAY;
        ESP_LOGI(TAG, "Setting 120Hz timing registers: HDelay=0x%02X, VDelay=0x%02X",
                 h_delay, v_delay);
    } else {
        h_delay = TIMING_60HZ_H_DELAY;
        v_delay = TIMING_60HZ_V_DELAY;
        ESP_LOGI(TAG, "Setting 60Hz timing registers: HDelay=0x%02X, VDelay=0x%02X",
                 h_delay, v_delay);
    }
    spi_write_reg(dev->spi, LCOS_REG_H_DELAY, h_delay);
    vTaskDelay(pdMS_TO_TICKS(5));
    spi_write_reg(dev->spi, LCOS_REG_V_DELAY, v_delay);
    vTaskDelay(pdMS_TO_TICKS(5));

    // 6. Verify registers
    ESP_LOGI(TAG, "Verifying Register Configuration:");
    int errors = 0;
    for (size_t i = 0; i < lcos_default_regs_count; i++) {
        uint8_t val = spi_read_reg(dev->spi, lcos_default_regs[i].addr);
        uint8_t expected = lcos_default_regs[i].value;
        // Use refresh-rate specific expected values for timing registers
        if (lcos_default_regs[i].addr == LCOS_REG_H_DELAY) {
            expected = h_delay;
        } else if (lcos_default_regs[i].addr == LCOS_REG_V_DELAY) {
            expected = v_delay;
        }
        if (val != expected) {
            ESP_LOGE(TAG, "  [FAIL] Reg 0x%02X (%s): Exp 0x%02X, Read 0x%02X",
                     lcos_default_regs[i].addr, lcos_default_regs[i].name,
                     expected, val);
            errors++;
        } else {
            ESP_LOGI(TAG, "  [OK]   Reg 0x%02X (%s): 0x%02X",
                     lcos_default_regs[i].addr, lcos_default_regs[i].name, val);
        }
    }

    // Read hardware ID
    dev->hw_revision = spi_read_reg(dev->spi, LCOS_REG_HW_CFG);
    ESP_LOGI(TAG, "  [INFO] Hardware Config (0x18): 0x%02X (Expected 0x%02X)",
             dev->hw_revision, LCOS_HW_ID_EXPECTED);

    if (errors > 0) {
        ESP_LOGW(TAG, "Register verification had %d errors", errors);
    }

    dev->initialized = true;
    dev->awake = false;
    *handle = dev;

    ESP_LOGI(TAG, "LCoS 201k initialized successfully");
    return ESP_OK;
}

esp_err_t lcos_deinit(lcos_handle_t handle) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    struct lcos_dev_s *dev = handle;

    // Put to sleep
    if (dev->awake) {
        lcos_sleep(handle);
    }

    // Hold reset
    gpio_set_level(dev->config.pins.nreset_gpio, 0);

    // Disable VCC
    if (dev->config.pins.vcc_ena_gpio >= 0) {
        gpio_set_level(dev->config.pins.vcc_ena_gpio, 0);
    }

    // Free SPI
    if (dev->spi) {
        spi_bus_remove_device(dev->spi);
        spi_bus_free(dev->config.pins.spi_host);
    }

    // Free LCD
    if (dev->panel) {
        esp_lcd_panel_del(dev->panel);
    }

    // Free semaphore
    if (dev->vsync_sem) {
        vSemaphoreDelete(dev->vsync_sem);
    }

    free(dev);
    return ESP_OK;
}

esp_err_t lcos_wake(lcos_handle_t handle) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    struct lcos_dev_s *dev = handle;

    // Build VidCfg3 value
    uint8_t cfg3 = LCOS_VID_CFG3_NSLEEP;
    if (dev->config.display.h_flip) cfg3 |= LCOS_VID_CFG3_HFLIP;
    if (dev->config.display.v_flip) cfg3 |= LCOS_VID_CFG3_VFLIP;

    ESP_LOGI(TAG, "Waking display (VidCfg3=0x%02X)...", cfg3);
    spi_write_reg(dev->spi, LCOS_REG_VID_CFG3, cfg3);

    // Verify
    uint8_t readback = spi_read_reg(dev->spi, LCOS_REG_VID_CFG3);
    if (readback == cfg3) {
        ESP_LOGI(TAG, "  [OK]   Display woke up (Reg 0x06: 0x%02X)", readback);
        dev->awake = true;
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "  [FAIL] Display wake issue (Reg 0x06: 0x%02X)", readback);
        return ESP_FAIL;
    }
}

esp_err_t lcos_sleep(lcos_handle_t handle) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    struct lcos_dev_s *dev = handle;

    ESP_LOGI(TAG, "Putting display to sleep...");
    spi_write_reg(dev->spi, LCOS_REG_VID_CFG3, 0x00);
    dev->awake = false;

    return ESP_OK;
}

esp_err_t lcos_get_framebuffer(lcos_handle_t handle, void **fb) {
    if (handle == NULL || fb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    struct lcos_dev_s *dev = handle;
    *fb = dev->framebuffer[dev->back_buffer_idx];
    return ESP_OK;
}

esp_err_t lcos_get_info(lcos_handle_t handle, lcos_info_t *info) {
    if (handle == NULL || info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    struct lcos_dev_s *dev = handle;

    info->width_pixels = LCOS_WIDTH;
    info->height_pixels = LCOS_HEIGHT;
    info->hw_revision = dev->hw_revision;
    info->is_awake = dev->awake;
    info->double_buffer_enabled = dev->double_buffer;
    info->frame_count = dev->frame_count;

    return ESP_OK;
}

esp_err_t lcos_set_brightness(lcos_handle_t handle, uint8_t brightness) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    struct lcos_dev_s *dev = handle;

    // Brightness is 6 bits (0-63), upper 2 bits must be 11
    uint8_t val = 0xC0 | (brightness & 0x3F);
    spi_write_reg(dev->spi, LCOS_REG_BRIGHTNESS, val);

    return ESP_OK;
}

esp_err_t lcos_set_flip(lcos_handle_t handle, bool h_flip, bool v_flip) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    struct lcos_dev_s *dev = handle;

    dev->config.display.h_flip = h_flip;
    dev->config.display.v_flip = v_flip;

    // If awake, update register
    if (dev->awake) {
        uint8_t cfg3 = LCOS_VID_CFG3_NSLEEP;
        if (h_flip) cfg3 |= LCOS_VID_CFG3_HFLIP;
        if (v_flip) cfg3 |= LCOS_VID_CFG3_VFLIP;
        spi_write_reg(dev->spi, LCOS_REG_VID_CFG3, cfg3);
    }

    return ESP_OK;
}

esp_err_t lcos_set_gamma(lcos_handle_t handle, uint8_t gamma) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    struct lcos_dev_s *dev = handle;
    spi_write_reg(dev->spi, LCOS_REG_GAMMA, gamma & 0x0F);

    return ESP_OK;
}

esp_err_t lcos_read_register(lcos_handle_t handle, uint8_t addr, uint8_t *value) {
    if (handle == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    struct lcos_dev_s *dev = handle;
    *value = spi_read_reg(dev->spi, addr);

    return ESP_OK;
}

esp_err_t lcos_write_register(lcos_handle_t handle, uint8_t addr, uint8_t value) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    struct lcos_dev_s *dev = handle;
    spi_write_reg(dev->spi, addr, value);

    return ESP_OK;
}

// --- Double Buffering Functions ---

esp_err_t lcos_present(lcos_handle_t handle, bool wait) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    struct lcos_dev_s *dev = handle;

    if (!dev->double_buffer) {
        // Single buffer mode - nothing to swap, but still useful to sync
        if (wait && dev->vsync_sem) {
            xSemaphoreTake(dev->vsync_sem, 0);  // Clear any pending
            xSemaphoreTake(dev->vsync_sem, pdMS_TO_TICKS(50));
        }
        return ESP_OK;
    }

    // Clear semaphore in case of spurious signals
    xSemaphoreTake(dev->vsync_sem, 0);

    // Tell LCD driver to display the back buffer
    // This queues a buffer swap at next vsync
    esp_lcd_panel_draw_bitmap(dev->panel, 0, 0, DISP_H_RES, DISP_V_RES,
                               dev->framebuffer[dev->back_buffer_idx]);

    // Swap indices - back becomes front, front becomes back
    dev->back_buffer_idx = 1 - dev->back_buffer_idx;

    if (wait) {
        // Block until vsync signals swap complete
        if (xSemaphoreTake(dev->vsync_sem, pdMS_TO_TICKS(50)) != pdTRUE) {
            ESP_LOGW(TAG, "Vsync wait timeout");
            return ESP_ERR_TIMEOUT;
        }
    }

    return ESP_OK;
}

esp_err_t lcos_wait_vsync(lcos_handle_t handle, uint32_t timeout_ms) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    struct lcos_dev_s *dev = handle;

    if (dev->vsync_sem == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // Clear any pending signal
    xSemaphoreTake(dev->vsync_sem, 0);

    TickType_t ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);

    if (xSemaphoreTake(dev->vsync_sem, ticks) == pdTRUE) {
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t lcos_set_vsync_callback(lcos_handle_t handle,
                                   lcos_vsync_cb_t callback,
                                   void *user_data) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    struct lcos_dev_s *dev = handle;

    // Set user_data first, then callback (order matters for ISR safety)
    dev->vsync_user_data = user_data;
    __sync_synchronize();  // Memory barrier
    dev->vsync_cb = callback;

    return ESP_OK;
}
