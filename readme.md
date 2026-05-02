The goal of this project is to create a device that shows a live follower/subscriber count for various SoMe platforms. 

Initially, i just want to start with a tiktok follower account. 

The hardware:
Esp32 30p wroom 32 dev board
2.25-inch TFT LCD ST7789p3    https://www.aliexpress.com/item/1005009347926783.html?spm=a2g0o.order_list.order_list_main.65.23841802OVCkJi

Display Resolution 76*284


This ST7789 display module is wired as an SPI display, even though the board labels the pins as SCL and SDA.

### Final confirmed wiring
- GND -> ESP32 GND
- VCC -> ESP32 3.3V
- SCL -> SPI clock (SCK) on ESP32, e.g. GPIO18
- SDA -> SPI data out (MOSI) on ESP32, e.g. GPIO23
- RST -> reset pin on ESP32, e.g. GPIO4
- DC  -> data/command pin on ESP32, e.g. GPIO2
- CS  -> chip select pin on ESP32, e.g. GPIO5
- BL  -> backlight pin
- On this module, `BL` is active low
- `BL = GND` turns the backlight on
- For control from ESP32, use a GPIO or transistor to drive it low/high safely

> Important: this display is not an I2C device, so do not treat SCL/SDA as I2C lines and do not add I2C pull-ups for normal SPI operation.

Note: some cheap modules label the SPI pins as `SCL` / `SDA`, but here they are actually SPI signals (clock and MOSI). The correct wiring above is the decided pinout for this board.

### Final confirmed display settings
- Controller: ST7789 / ST7789P3
- Visible resolution: `76 x 284`
- Color format: RGB565 / 16-bit color
- SPI mode: `0`
- Tested SPI clock: `10 MHz`
- MADCTL/orientation value: `0x00`
- Column/X offset: `82`
- Row/Y offset: `18`

These settings were found with the ESP-IDF display test. In `src/main.c`, the working values are:

```c
#define TFT_WIDTH 76
#define TFT_HEIGHT 284
#define TFT_X_OFFSET 82
#define TFT_Y_OFFSET 18
#define TFT_MADCTL 0x00
```

I would like to run ESP32 with RTOS for single-core multitask handling,
using ESP-IDF coding in VS Code.

# SoMe_Stat_Box
