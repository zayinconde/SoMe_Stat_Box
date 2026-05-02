#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TFT_HOST SPI2_HOST

#define TFT_MOSI_GPIO 23
#define TFT_SCLK_GPIO 18
#define TFT_CS_GPIO 5
#define TFT_DC_GPIO 2
#define TFT_RST_GPIO 4

/*
 * BL is active low on this module. If you wire BL directly to GND, leave this
 * at -1. If you wire BL to a GPIO, set that GPIO number here.
 */
#define TFT_BL_GPIO (-1)

#define TFT_WIDTH 76
#define TFT_HEIGHT 284

#define TFT_X_OFFSET 82
#define TFT_Y_OFFSET 18
#define TFT_MADCTL 0x00

#define ST7789_SWRESET 0x01
#define ST7789_SLPOUT 0x11
#define ST7789_NORON 0x13
#define ST7789_INVOFF 0x20
#define ST7789_INVON 0x21
#define ST7789_CASET 0x2A
#define ST7789_RASET 0x2B
#define ST7789_RAMWR 0x2C
#define ST7789_MADCTL 0x36
#define ST7789_COLMOD 0x3A
#define ST7789_DISPON 0x29

#define COLOR_BLACK 0x0000
#define COLOR_WHITE 0xFFFF
#define COLOR_RED 0xF800
#define COLOR_GREEN 0x07E0
#define COLOR_BLUE 0x001F
#define COLOR_YELLOW 0xFFE0
#define COLOR_CYAN 0x07FF
#define COLOR_MAGENTA 0xF81F
#define COLOR_ORANGE 0xFD20

static const char *TAG = "display_test";
static spi_device_handle_t s_tft;

static uint16_t rgb565_be(uint16_t color)
{
    return (uint16_t)((color << 8) | (color >> 8));
}

static esp_err_t tft_send_cmd(uint8_t cmd)
{
    spi_transaction_t tx = {
        .length = 8,
        .tx_buffer = &cmd,
    };

    gpio_set_level(TFT_DC_GPIO, 0);
    return spi_device_polling_transmit(s_tft, &tx);
}

static esp_err_t tft_send_data(const void *data, size_t len)
{
    if (len == 0) {
        return ESP_OK;
    }

    spi_transaction_t tx = {
        .length = len * 8,
        .tx_buffer = data,
    };

    gpio_set_level(TFT_DC_GPIO, 1);
    return spi_device_polling_transmit(s_tft, &tx);
}

static esp_err_t tft_write_cmd_data(uint8_t cmd, const void *data, size_t len)
{
    ESP_RETURN_ON_ERROR(tft_send_cmd(cmd), TAG, "command 0x%02x failed", cmd);
    return tft_send_data(data, len);
}

static esp_err_t tft_set_window(int x, int y, int width, int height)
{
    const uint16_t x_start = x + TFT_X_OFFSET;
    const uint16_t x_end = x_start + width - 1;
    const uint16_t y_start = y + TFT_Y_OFFSET;
    const uint16_t y_end = y_start + height - 1;

    const uint8_t caset[] = {
        (uint8_t)(x_start >> 8), (uint8_t)x_start,
        (uint8_t)(x_end >> 8), (uint8_t)x_end,
    };
    const uint8_t raset[] = {
        (uint8_t)(y_start >> 8), (uint8_t)y_start,
        (uint8_t)(y_end >> 8), (uint8_t)y_end,
    };

    ESP_RETURN_ON_ERROR(tft_write_cmd_data(ST7789_CASET, caset, sizeof(caset)), TAG, "CASET failed");
    ESP_RETURN_ON_ERROR(tft_write_cmd_data(ST7789_RASET, raset, sizeof(raset)), TAG, "RASET failed");
    return tft_send_cmd(ST7789_RAMWR);
}

static esp_err_t tft_fill_rect(int x, int y, int width, int height, uint16_t color)
{
    enum { PIXELS_PER_CHUNK = 256 };
    uint16_t pixels[PIXELS_PER_CHUNK];
    int remaining = width * height;

    if (x < 0 || y < 0 || width <= 0 || height <= 0 ||
        x + width > TFT_WIDTH || y + height > TFT_HEIGHT) {
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < PIXELS_PER_CHUNK; i++) {
        pixels[i] = rgb565_be(color);
    }

    ESP_RETURN_ON_ERROR(tft_set_window(x, y, width, height), TAG, "set window failed");

    while (remaining > 0) {
        const int chunk = remaining > PIXELS_PER_CHUNK ? PIXELS_PER_CHUNK : remaining;
        ESP_RETURN_ON_ERROR(tft_send_data(pixels, chunk * sizeof(uint16_t)), TAG, "pixel write failed");
        remaining -= chunk;
    }

    return ESP_OK;
}

static esp_err_t tft_init_panel(void)
{
    const uint8_t color_mode = 0x55; /* 16-bit RGB565. */
    const uint8_t madctl = TFT_MADCTL;

#if TFT_BL_GPIO >= 0
    gpio_config_t bl_config = {
        .pin_bit_mask = 1ULL << TFT_BL_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&bl_config), TAG, "backlight GPIO config failed");
    gpio_set_level(TFT_BL_GPIO, 0);
#endif

    gpio_set_level(TFT_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(TFT_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    ESP_RETURN_ON_ERROR(tft_send_cmd(ST7789_SWRESET), TAG, "software reset failed");
    vTaskDelay(pdMS_TO_TICKS(150));
    ESP_RETURN_ON_ERROR(tft_send_cmd(ST7789_SLPOUT), TAG, "sleep out failed");
    vTaskDelay(pdMS_TO_TICKS(120));
    ESP_RETURN_ON_ERROR(tft_write_cmd_data(ST7789_COLMOD, &color_mode, sizeof(color_mode)), TAG, "color mode failed");
    ESP_RETURN_ON_ERROR(tft_write_cmd_data(ST7789_MADCTL, &madctl, sizeof(madctl)), TAG, "MADCTL failed");
    ESP_RETURN_ON_ERROR(tft_send_cmd(ST7789_INVON), TAG, "inversion on failed");
    ESP_RETURN_ON_ERROR(tft_send_cmd(ST7789_NORON), TAG, "normal display failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(tft_send_cmd(ST7789_DISPON), TAG, "display on failed");
    vTaskDelay(pdMS_TO_TICKS(120));

    return ESP_OK;
}

static esp_err_t tft_bus_init(void)
{
    gpio_config_t output_config = {
        .pin_bit_mask = (1ULL << TFT_DC_GPIO) | (1ULL << TFT_RST_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&output_config), TAG, "GPIO config failed");

    spi_bus_config_t bus_config = {
        .mosi_io_num = TFT_MOSI_GPIO,
        .miso_io_num = -1,
        .sclk_io_num = TFT_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = TFT_WIDTH * TFT_HEIGHT * sizeof(uint16_t),
    };

    spi_device_interface_config_t device_config = {
        .clock_speed_hz = 10 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = TFT_CS_GPIO,
        .queue_size = 1,
    };

    ESP_RETURN_ON_ERROR(spi_bus_initialize(TFT_HOST, &bus_config, SPI_DMA_CH_AUTO), TAG, "SPI bus init failed");
    return spi_bus_add_device(TFT_HOST, &device_config, &s_tft);
}

static void draw_display_test_pattern(void)
{
    const uint16_t bars[] = {
        COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_YELLOW,
        COLOR_CYAN, COLOR_MAGENTA, COLOR_WHITE,
    };
    const int bar_count = sizeof(bars) / sizeof(bars[0]);
    const int bar_height = TFT_HEIGHT / bar_count;

    for (int i = 0; i < bar_count; i++) {
        const int y = i * bar_height;
        const int height = (i == bar_count - 1) ? TFT_HEIGHT - y : bar_height;
        ESP_ERROR_CHECK(tft_fill_rect(0, y, TFT_WIDTH, height, bars[i]));
    }

    ESP_ERROR_CHECK(tft_fill_rect(0, 0, TFT_WIDTH, 4, COLOR_WHITE));
    ESP_ERROR_CHECK(tft_fill_rect(0, TFT_HEIGHT - 4, TFT_WIDTH, 4, COLOR_WHITE));
    ESP_ERROR_CHECK(tft_fill_rect(0, 0, 4, TFT_HEIGHT, COLOR_WHITE));
    ESP_ERROR_CHECK(tft_fill_rect(TFT_WIDTH - 4, 0, 4, TFT_HEIGHT, COLOR_WHITE));
}

static void display_test_task(void *arg)
{
    (void)arg;

    const uint16_t full_screen_colors[] = {
        COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_BLACK,
    };
    const int color_count = sizeof(full_screen_colors) / sizeof(full_screen_colors[0]);
    int color_index = 0;

    ESP_LOGI(TAG, "Display test running with box 10 settings: x=%d y=%d madctl=0x%02x",
             TFT_X_OFFSET, TFT_Y_OFFSET, TFT_MADCTL);

    while (true) {
        ESP_ERROR_CHECK(tft_fill_rect(0, 0, TFT_WIDTH, TFT_HEIGHT, full_screen_colors[color_index]));
        vTaskDelay(pdMS_TO_TICKS(1200));

        draw_display_test_pattern();
        vTaskDelay(pdMS_TO_TICKS(2500));

        color_index = (color_index + 1) % color_count;
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(tft_bus_init());
    ESP_ERROR_CHECK(tft_init_panel());

    ESP_LOGI(TAG, "ST7789 test started: %dx%d, offsets x=%d y=%d", TFT_WIDTH, TFT_HEIGHT,
             TFT_X_OFFSET, TFT_Y_OFFSET);
    xTaskCreate(display_test_task, "display_test", 4096, NULL, 5, NULL);
}
