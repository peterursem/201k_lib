#ifndef LCOS_201K_H
#define LCOS_201K_H

/**
 * @file lcos_201k.h
 * @brief ESP-IDF driver for Displaytech LightView 201k LCoS display (LDM-0201-E3)
 *
 * This library provides a high-level interface to initialize and control the
 * 300x224 Field Sequential Color LCoS display module.
 *
 * Basic usage:
 * @code
 * lcos_config_t config;
 * lcos_get_default_config(&config);
 *
 * lcos_handle_t display;
 * lcos_init(&config, &display);
 * lcos_wake(display);
 *
 * void *fb;
 * lcos_get_framebuffer(display, &fb);
 * // Draw to framebuffer (900 x 224 bytes, RGB serial format)
 * @endcode
 */

#include "lcos_201k_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get default configuration with standard pin assignments
 *
 * Fills config struct with defaults matching the documented pinout.
 * Use this as a starting point and modify as needed.
 *
 * @param[out] config Configuration struct to fill
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if config is NULL
 */
esp_err_t lcos_get_default_config(lcos_config_t *config);

/**
 * @brief Initialize the LCoS display
 *
 * Performs the full initialization sequence:
 * 1. Hold NRESET low
 * 2. Initialize RGB LCD peripheral (provides clock)
 * 3. Release NRESET, wait for tSDLY
 * 4. Initialize SPI and configure registers
 * 5. Verify register configuration
 *
 * Note: Call lcos_wake() after init to turn on the display.
 *
 * @param[in]  config Configuration parameters
 * @param[out] handle Handle to initialized device
 * @return ESP_OK on success
 */
esp_err_t lcos_init(const lcos_config_t *config, lcos_handle_t *handle);

/**
 * @brief Deinitialize the LCoS display
 *
 * Puts display to sleep, releases SPI and LCD resources.
 *
 * @param[in] handle Device handle
 * @return ESP_OK on success
 */
esp_err_t lcos_deinit(lcos_handle_t handle);

/**
 * @brief Wake the display from sleep mode
 *
 * Sets NSleepMode=1 and applies h_flip/v_flip settings from config.
 *
 * @param[in] handle Device handle
 * @return ESP_OK on success
 */
esp_err_t lcos_wake(lcos_handle_t handle);

/**
 * @brief Put the display into sleep mode
 *
 * @param[in] handle Device handle
 * @return ESP_OK on success
 */
esp_err_t lcos_sleep(lcos_handle_t handle);

/**
 * @brief Get framebuffer pointer for drawing
 *
 * Returns pointer to the framebuffer allocated by the LCD driver.
 * The framebuffer is 900 x 224 bytes (RGB serial format).
 *
 * In double-buffer mode, returns the back buffer (not currently displayed).
 * Draw to this buffer, then call lcos_present() to swap.
 *
 * @param[in]  handle Device handle
 * @param[out] fb     Pointer to framebuffer
 * @return ESP_OK on success
 */
esp_err_t lcos_get_framebuffer(lcos_handle_t handle, void **fb);

/**
 * @brief Present the back buffer (double-buffer mode)
 *
 * Swaps the back buffer to front at the next vsync, eliminating tearing.
 * In single-buffer mode, this is a no-op and returns immediately.
 *
 * @param[in] handle Device handle
 * @param[in] wait   If true, block until swap completes at vsync
 * @return ESP_OK on success
 */
esp_err_t lcos_present(lcos_handle_t handle, bool wait);

/**
 * @brief Wait for vertical sync
 *
 * Blocks until next vertical blank period. Useful for timing without
 * double buffering, or for synchronizing other operations to display refresh.
 *
 * @param[in] handle     Device handle
 * @param[in] timeout_ms Maximum wait time in ms (0 = wait forever)
 * @return ESP_OK on vsync, ESP_ERR_TIMEOUT on timeout
 */
esp_err_t lcos_wait_vsync(lcos_handle_t handle, uint32_t timeout_ms);

/**
 * @brief Set vsync callback
 *
 * Callback is invoked from ISR context after each vsync.
 * Keep callback fast - just set a flag or post to a queue.
 *
 * @param[in] handle    Device handle
 * @param[in] callback  Callback function (NULL to disable)
 * @param[in] user_data Context passed to callback
 * @return ESP_OK on success
 */
esp_err_t lcos_set_vsync_callback(lcos_handle_t handle,
                                   lcos_vsync_cb_t callback,
                                   void *user_data);

/**
 * @brief Get display information
 *
 * @param[in]  handle Device handle
 * @param[out] info   Display information struct
 * @return ESP_OK on success
 */
esp_err_t lcos_get_info(lcos_handle_t handle, lcos_info_t *info);

/**
 * @brief Set LED brightness
 *
 * @param[in] handle     Device handle
 * @param[in] brightness Brightness level 0-63
 * @return ESP_OK on success
 */
esp_err_t lcos_set_brightness(lcos_handle_t handle, uint8_t brightness);

/**
 * @brief Set horizontal/vertical flip
 *
 * @param[in] handle Device handle
 * @param[in] h_flip Enable horizontal flip
 * @param[in] v_flip Enable vertical flip
 * @return ESP_OK on success
 */
esp_err_t lcos_set_flip(lcos_handle_t handle, bool h_flip, bool v_flip);

/**
 * @brief Set gamma correction
 *
 * @param[in] handle Device handle
 * @param[in] gamma  Gamma value 0-15 (9 = 2.1 default)
 * @return ESP_OK on success
 */
esp_err_t lcos_set_gamma(lcos_handle_t handle, uint8_t gamma);

/**
 * @brief Read a display register
 *
 * For debugging and diagnostics.
 *
 * @param[in]  handle Device handle
 * @param[in]  addr   Register address (0x00-0x1F)
 * @param[out] value  Register value
 * @return ESP_OK on success
 */
esp_err_t lcos_read_register(lcos_handle_t handle, uint8_t addr, uint8_t *value);

/**
 * @brief Write a display register
 *
 * For advanced use only. Most settings should use the dedicated functions.
 *
 * @param[in] handle Device handle
 * @param[in] addr   Register address (0x00-0x1F)
 * @param[in] value  Value to write
 * @return ESP_OK on success
 */
esp_err_t lcos_write_register(lcos_handle_t handle, uint8_t addr, uint8_t value);

#ifdef __cplusplus
}
#endif

#endif // LCOS_201K_H
