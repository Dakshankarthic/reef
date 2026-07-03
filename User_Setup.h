#define USER_SETUP_LOADED 1

#define ST7796_DRIVER
#define TOUCH_XPT2046

#define TFT_WIDTH 320
#define TFT_HEIGHT 480

#define TFT_MOSI 13
#define TFT_MISO 12
#define TFT_SCLK 14
#define TFT_CS 4
#define TFT_DC 9
#define TFT_RST 15
#define TFT_BL 11

// ----- XPT2046 touch (shared SPI2 bus, own CS) -----
#define TOUCH_CS  18  // Touch CS  -> XPT2046 CS
#define TOUCH_IRQ 21 // Touch IRQ -> XPT2046 IRQ (optional)

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

#define SPI_FREQUENCY 10000000
#define SPI_READ_FREQUENCY 10000000
#define SPI_TOUCH_FREQUENCY 1000000
