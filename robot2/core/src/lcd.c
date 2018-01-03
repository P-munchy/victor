#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

#include "core/common.h"
#include "core/clock.h"
#include "core/gpio.h"

#include "core/lcd.h"

#define FALSE 0
#define TRUE (!FALSE)


static const int DAT_CLOCK = 17500000;
static const int MAX_TRANSFER = 0x1000;


#define GPIO_LCD_WRX   110
#define GPIO_LCD_RESET1 96
#define GPIO_LCD_RESET2 55

static GPIO RESET_PIN1;
static GPIO RESET_PIN2;
static GPIO DnC_PIN;


#define RSHIFT 0x1C

typedef struct {
  uint8_t cmd;
  uint8_t data_bytes;
  uint8_t data[14];
} INIT_SCRIPT;


static const INIT_SCRIPT init_scr[] = {
  { 0x11, 0 }, // Sleep Off
  { 0x36, 1, { 0x00 } },
  { 0xB7, 1, { 0x72 } }, // Gate control (VGH 14.97, VGL -8.23)
  { 0xBB, 1, { 0x36 } }, // VCOMS 1.45v
  { 0xC0, 1, { 0x2C } },
  { 0xC2, 1, { 0x01 } },
  { 0xC3, 1, { 0x14 } }, // VRH 4.55v
  { 0xC4, 1, { 0x20 } },
  { 0xC6, 1, { 0x0F } },
  { 0xD0, 2, { 0xA4, 0xA1 } }, // Power control 1
  { 0xE0, 14, { 0xD0, 0x10, 0x16, 0x0A, 0x0A, 0x26, 0x3C, 0x53, 0x53, 0x18, 0x15, 0x12, 0x36, 0x3C } }, // +ve voltage gamma control
  { 0xE1, 14, { 0xD0, 0x11, 0x19, 0x0A, 0x09, 0x25, 0x3D, 0x35, 0x54, 0x17, 0x15, 0x12, 0x36, 0x3C } }, // -ve voltage gamma control
  { 0x3A, 1, { 0x55 } },
  { 0x55, 1, { 0x03 } }, // Content Adaptive Brightness Control: 0x03 = Color Enhancement Off, Moving Image Mode 
  { 0x21, 0 },
  { 0x2A, 4, { 0x00, RSHIFT, (LCD_FRAME_WIDTH + RSHIFT - 1) >> 8, (LCD_FRAME_WIDTH + RSHIFT - 1) & 0xFF } },
  { 0x2B, 4, { 0x00, 0x00, (LCD_FRAME_HEIGHT -1) >> 8, (LCD_FRAME_HEIGHT -1) & 0xFF } },
  { 0x26, 1, { 0x08 } }, // Gamma Curve Setting: 0x01=2.2, 0x02=1.8, 0x04=2.5, 0x08=1.0 
  //{ 0x53, 1, {0x24} },  // Brightness control: Brightness registers active, no dimming, backlight on
  //{ 0x51, 1, {0x80} },  // Screen brightness value
  { 0x29, 0 }, // Display On
  
  { 0 }
};



/************* LCD SPI Interface ***************/


static int spi_fd;

static int lcd_spi_init()
{
  // SPI setup
  static const uint8_t    MODE = 0;
  int spi_fd = open("/dev/spidev1.0", O_RDWR);
  if (!spi_fd) {
    error_exit(app_DEVICE_OPEN_ERROR, "Can't open LCD SPI interface\n");
  }
  ioctl(spi_fd, SPI_IOC_RD_MODE, &MODE);
  /* if (err<0) { */
  /*   error_exit(app_IO_ERROR, "Can't configure LCD SPI. (%d)\n", errno); */
  /* } */
  return spi_fd;
}

static void lcd_spi_transfer(int cmd, int bytes, const void* data) {
  const uint8_t* tx_buf = data;

  gpio_set_value(DnC_PIN, cmd ? gpio_LOW : gpio_HIGH);

  while (bytes > 0) {
    const size_t count = bytes > MAX_TRANSFER ? MAX_TRANSFER : bytes;
    
    write(spi_fd, tx_buf, count);
    
    bytes -= count;
    tx_buf += count;
  }
}

/************ LCD Device Interface *************/

static void lcd_device_init() {
  int idx;

  for (idx = 0; init_scr[idx].cmd; idx++) {
    lcd_spi_transfer(TRUE, 1, &init_scr[idx].cmd);
    lcd_spi_transfer(FALSE, init_scr[idx].data_bytes, init_scr[idx].data);
  }
}

void lcd_clear_screen(void) {
  const LcdFrame frame={{0}};
  lcd_draw_frame(&frame);
}

void lcd_draw_frame(const LcdFrame* frame) {
   static const uint8_t WRITE_RAM = 0x2C;
   lcd_spi_transfer(TRUE, 1, &WRITE_RAM);
   lcd_spi_transfer(FALSE, sizeof(frame->data), frame->data);
}

void lcd_draw_frame2(const uint16_t* frame, size_t size) {
   static const uint8_t WRITE_RAM = 0x2C;
   lcd_spi_transfer(TRUE, 1, &WRITE_RAM);
   lcd_spi_transfer(FALSE, size, frame);
}

#define MAX(a,b) (((a)>(b))?(a):(b))
#define MIN(a,b) (((a)<(b))?(a):(b))

void lcd_set_brightness(int brightness)
{
  brightness = MIN(brightness, 20);
  brightness = MAX(brightness, 0);
  int fd = open("/sys/class/leds/face-backlight/brightness",O_WRONLY);
  if (fd) {
    char buf[3];
    snprintf(buf,3,"%02d\n",brightness);
    write(fd, buf, 3);
    close(fd);
  }
}


int lcd_init(void) {

//TODO : BACKLIGHT and REGULATOR INTERFACE
  // Echo to device to activate backlight
  system("echo 1 > /sys/kernel/debug/regulator/8916_l17/enable");
  system("echo 1 > /sys/kernel/debug/regulator/8916_l4/enable");

  lcd_set_brightness(10);

  // IO Setup
  DnC_PIN = gpio_create(GPIO_LCD_WRX, gpio_DIR_OUTPUT, gpio_HIGH);

  RESET_PIN1 = gpio_create_open_drain_output(GPIO_LCD_RESET1, gpio_HIGH);
  RESET_PIN2 = gpio_create_open_drain_output(GPIO_LCD_RESET2, gpio_HIGH);

  // SPI setup

  spi_fd = lcd_spi_init();

  // Send reset signal
  microwait(50);
  gpio_set_value(RESET_PIN1, 0);
  gpio_set_value(RESET_PIN2, 0);
  microwait(50);
  gpio_set_value(RESET_PIN1, 1);
  gpio_set_value(RESET_PIN2, 1);
  microwait(50);

  lcd_device_init();

  lcd_clear_screen();

  return 0;
}


void lcd_shutdown(void) {
  //todo: turn off screen?
  system("echo 0 > /sys/kernel/debug/regulator/8916_l17/enable");
  system("echo 0 > /sys/kernel/debug/regulator/8916_l4/enable");

  if (spi_fd) {
    static const uint8_t SLEEP = 0x10;
    lcd_spi_transfer(TRUE, 1, &SLEEP);
    close(spi_fd);
  }
  if (DnC_PIN) {
    gpio_close(DnC_PIN);
  }
  if (RESET_PIN1) {
    gpio_close(RESET_PIN1);
  }
  if (RESET_PIN2) {
    gpio_close(RESET_PIN2);
  }

}
