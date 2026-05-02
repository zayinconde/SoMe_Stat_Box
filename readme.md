The goal of this project is to create a device that shows a live follower/subscriber count for various SoMe platforms. 

Initially, i just want to start with a tiktok follower account. 

The hardware:
Esp32 30p wroom 32 dev board
2.25-inch TFT LCD ST7789p3


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

I would like to run ESP32 with RTOS for single-core multitask handling,
using ESP-IDF coding in VS Code.

# SoMe_Stat_Box
