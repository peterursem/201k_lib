#ifndef LCOS_201K_TYPES_H
#define LCOS_201K_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/spi_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LCoS display handle (opaque pointer)
 */
typedef struct lcos_dev_s *lcos_handle_t;

/**
 * @brief Display refresh rate options
 *
 * Note: Despite the name, LCOS_REFRESH_HIGH achieves ~62Hz (not 120Hz).
 * The display hardware limits maximum input frame rate to ~62Hz due to
 * timing constraints discovered during testing.
 */
typedef enum {
    LCOS_REFRESH_60HZ = 0,      ///< ~59Hz refresh (default, standard timing)
    LCOS_REFRESH_HIGH = 1,      ///< ~62Hz refresh (reduced blanking, max achievable)
} lcos_refresh_rate_t;

/**
 * @brief Pin configuration for the LCoS display
 */
typedef struct {
    // Video interface (RGB)
    int pclk_gpio;              ///< Pixel clock GPIO (default: 18)
    int vsync_gpio;             ///< Vertical sync GPIO (default: 3)
    int hsync_gpio;             ///< Horizontal sync GPIO (default: 8)
    int de_gpio;                ///< Data enable GPIO, -1 for internal VALID (default: -1)
    int data_gpio[8];           ///< Data bus D0-D7 (defaults: 9,10,11,12,13,14,21,47)

    // Control interface
    int nreset_gpio;            ///< Hardware reset, active low (default: 46)
    int vcc_ena_gpio;           ///< VCC/LED enable, active high (default: 41), -1 to disable

    // SPI configuration interface
    int spi_cs_gpio;            ///< SPI chip select (default: 40)
    int spi_clk_gpio;           ///< SPI clock (default: 39)
    int spi_mosi_gpio;          ///< SPI MOSI (default: 48)
    int spi_miso_gpio;          ///< SPI MISO (default: 38)
    spi_host_device_t spi_host; ///< SPI host (default: SPI2_HOST)
} lcos_pin_config_t;

/**
 * @brief Display configuration options
 */
typedef struct {
    uint8_t brightness;         ///< LED brightness 0-63 (default: 63 = max)
    uint8_t gamma;              ///< Gamma setting 0-15 (default: 9 = 2.1)
    bool h_flip;                ///< Horizontal flip (default: false)
    bool v_flip;                ///< Vertical flip (default: false)
    bool double_buffer;         ///< Enable double buffering (default: false)
                                ///< When enabled, framebuffers are allocated in PSRAM
    lcos_refresh_rate_t refresh_rate;  ///< Refresh rate (default: LCOS_REFRESH_60HZ)
} lcos_display_config_t;

/**
 * @brief Vsync callback type (called from ISR context)
 * @param user_data User context passed to lcos_set_vsync_callback
 * @note Keep callback fast - just set a flag or post to a queue
 */
typedef void (*lcos_vsync_cb_t)(void *user_data);

/**
 * @brief Full device configuration
 */
typedef struct {
    lcos_pin_config_t pins;         ///< Pin assignments
    lcos_display_config_t display;  ///< Display options
} lcos_config_t;

/**
 * @brief Display information (read-only status)
 */
typedef struct {
    uint16_t width_pixels;      ///< Display width (300)
    uint16_t height_pixels;     ///< Display height (224)
    uint8_t hw_revision;        ///< Hardware revision from register 0x18
    bool is_awake;              ///< Current sleep state
    bool double_buffer_enabled; ///< True if double buffering is active
    uint32_t frame_count;       ///< Frames presented since init (for debugging)
} lcos_info_t;

/**
 * @brief Display constants
 */
#define LCOS_WIDTH              300     ///< Display width in pixels
#define LCOS_HEIGHT             224     ///< Display height in pixels
#define LCOS_FB_WIDTH           900     ///< Framebuffer width (300 * 3 for RGB serial)
#define LCOS_FB_SIZE            (LCOS_FB_WIDTH * LCOS_HEIGHT)  ///< Framebuffer size in bytes

#ifdef __cplusplus
}
#endif

#endif // LCOS_201K_TYPES_H
