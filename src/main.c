#include <stdint.h>
#include <stdio.h>

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

#define TFT_WIDTH 284
#define TFT_HEIGHT 76

#define TFT_X_OFFSET 18
#define TFT_Y_OFFSET 82
#define TFT_MADCTL 0x60

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
#define COLOR_TIKTOK_CYAN 0x067F
#define COLOR_TIKTOK_PINK 0xF81B
#define COLOR_DARK_GRAY 0x1082

static const char *TAG = "stat_box";
static spi_device_handle_t s_tft;

static const uint8_t s_font_5x7[][5] = {
    ['0'] = {0x3E, 0x51, 0x49, 0x45, 0x3E},
    ['1'] = {0x00, 0x42, 0x7F, 0x40, 0x00},
    ['2'] = {0x42, 0x61, 0x51, 0x49, 0x46},
    ['3'] = {0x21, 0x41, 0x45, 0x4B, 0x31},
    ['4'] = {0x18, 0x14, 0x12, 0x7F, 0x10},
    ['5'] = {0x27, 0x45, 0x45, 0x45, 0x39},
    ['6'] = {0x3C, 0x4A, 0x49, 0x49, 0x30},
    ['7'] = {0x01, 0x71, 0x09, 0x05, 0x03},
    ['8'] = {0x36, 0x49, 0x49, 0x49, 0x36},
    ['9'] = {0x06, 0x49, 0x49, 0x29, 0x1E},
    ['E'] = {0x7F, 0x49, 0x49, 0x49, 0x41},
    ['F'] = {0x7F, 0x09, 0x09, 0x09, 0x01},
    ['K'] = {0x7F, 0x08, 0x14, 0x22, 0x41},
    ['L'] = {0x7F, 0x40, 0x40, 0x40, 0x40},
    ['M'] = {0x7F, 0x02, 0x0C, 0x02, 0x7F},
    ['O'] = {0x3E, 0x41, 0x41, 0x41, 0x3E},
    ['R'] = {0x7F, 0x09, 0x19, 0x29, 0x46},
    ['S'] = {0x46, 0x49, 0x49, 0x49, 0x31},
    ['W'] = {0x7F, 0x20, 0x18, 0x20, 0x7F},
    ['.'] = {0x00, 0x60, 0x60, 0x00, 0x00},
};

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

static void tft_fill_rect_clipped(int x, int y, int width, int height, uint16_t color)
{
    if (x < 0) {
        width += x;
        x = 0;
    }
    if (y < 0) {
        height += y;
        y = 0;
    }
    if (x + width > TFT_WIDTH) {
        width = TFT_WIDTH - x;
    }
    if (y + height > TFT_HEIGHT) {
        height = TFT_HEIGHT - y;
    }
    if (width > 0 && height > 0) {
        ESP_ERROR_CHECK(tft_fill_rect(x, y, width, height, color));
    }
}

static void draw_filled_circle(int cx, int cy, int radius, uint16_t color)
{
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            if (x * x + y * y <= radius * radius) {
                tft_fill_rect_clipped(cx + x, cy + y, 1, 1, color);
            }
        }
    }
}

static void draw_text_5x7(const char *text, int x, int y, int scale, uint16_t color)
{
    while (*text != '\0') {
        const unsigned char ch = (unsigned char)*text++;

        if (ch == ' ') {
            x += 4 * scale;
            continue;
        }

        const uint8_t *glyph = s_font_5x7[ch];
        for (int col = 0; col < 5; col++) {
            for (int row = 0; row < 7; row++) {
                if ((glyph[col] & (1U << row)) != 0) {
                    tft_fill_rect_clipped(x + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }

        x += 6 * scale;
    }
}

static void draw_tiktok_note_layer(int x, int y, int offset_x, int offset_y, uint16_t color)
{
    const int ox = x + offset_x;
    const int oy = y + offset_y;

    tft_fill_rect_clipped(ox + 19, oy + 3, 8, 48, color);
    tft_fill_rect_clipped(ox + 27, oy + 10, 18, 7, color);
    tft_fill_rect_clipped(ox + 38, oy + 15, 7, 10, color);
    draw_filled_circle(ox + 14, oy + 52, 12, color);
    tft_fill_rect_clipped(ox + 14, oy + 40, 13, 15, color);
}

static void draw_tiktok_logo(int x, int y)
{
    draw_tiktok_note_layer(x, y, -4, 4, COLOR_TIKTOK_CYAN);
    draw_tiktok_note_layer(x, y, 4, -4, COLOR_TIKTOK_PINK);
    draw_tiktok_note_layer(x, y, 0, 0, COLOR_WHITE);
}

static void draw_tiktok_frame(void)
{
    ESP_ERROR_CHECK(tft_fill_rect(0, 0, TFT_WIDTH, TFT_HEIGHT, COLOR_BLACK));

    tft_fill_rect_clipped(0, 0, 6, TFT_HEIGHT, COLOR_TIKTOK_CYAN);
    tft_fill_rect_clipped(TFT_WIDTH - 6, 0, 6, TFT_HEIGHT, COLOR_TIKTOK_PINK);
    draw_tiktok_logo(19, 7);
    draw_text_5x7("FOLLOWERS", 96, 42, 1, COLOR_DARK_GRAY);
    tft_fill_rect_clipped(80, 64, 184, 3, COLOR_DARK_GRAY);
}

static void format_followers(char *buffer, size_t buffer_size, int value)
{
    if (value >= 1000) {
        snprintf(buffer, buffer_size, "%d.%03d", value / 1000, value % 1000);
    } else {
        snprintf(buffer, buffer_size, "%d", value);
    }
}

static void draw_tiktok_values(int followers, int likes)
{
    static char previous_follower_text[12] = "";
    static char previous_like_text[8] = "";
    char follower_text[12];
    char like_text[8];

    format_followers(follower_text, sizeof(follower_text), followers);
    snprintf(like_text, sizeof(like_text), "%d", likes);

    if (previous_follower_text[0] != '\0') {
        draw_text_5x7(previous_follower_text, 92, 13, 3, COLOR_BLACK);
    }
    if (previous_like_text[0] != '\0') {
        draw_text_5x7(previous_like_text, 214, 22, 2, COLOR_BLACK);
    }

    draw_text_5x7(follower_text, 92, 13, 3, COLOR_WHITE);
    draw_text_5x7(like_text, 214, 22, 2, COLOR_WHITE);

    snprintf(previous_follower_text, sizeof(previous_follower_text), "%s", follower_text);
    snprintf(previous_like_text, sizeof(previous_like_text), "%s", like_text);
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

static void stat_box_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "TikTok example screen running with box 10 settings: x=%d y=%d madctl=0x%02x",
             TFT_X_OFFSET, TFT_Y_OFFSET, TFT_MADCTL);
    draw_tiktok_frame();

    int followers = 3754;
    int likes = 204;

    while (true) {
        draw_tiktok_values(followers, likes);

        followers += 37;
        if (followers > 99900) {
            followers = 3754;
        }

        likes += 7;
        if (likes > 999) {
            likes = 204;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(tft_bus_init());
    ESP_ERROR_CHECK(tft_init_panel());

    ESP_LOGI(TAG, "ST7789 stat box started: %dx%d, offsets x=%d y=%d", TFT_WIDTH, TFT_HEIGHT,
             TFT_X_OFFSET, TFT_Y_OFFSET);
    xTaskCreate(stat_box_task, "stat_box", 4096, NULL, 5, NULL);
}
