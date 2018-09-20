#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <linux/fb.h>

#include "core/common.h"
#include "core/clock.h"
#include "core/gpio.h"

#include "core/lcd.h"

#define FALSE 0
#define TRUE (!FALSE)


static int MAX_TRANSFER = 0x1000;

static int lcd_use_fb; // use /dev/fb0?

#define GPIO_LCD_WRX   110
#define GPIO_LCD_RESET 55

static GPIO RESET_PIN;
static GPIO DnC_PIN;


#define RSHIFT 0x1C

typedef struct {
  uint8_t cmd;
  uint8_t data_bytes;
  uint8_t data[14];
  uint32_t delay_ms;
} INIT_SCRIPT;

static const INIT_SCRIPT init_scr[] = {
  { 0x10, 1, { 0x00 }, 120}, // Sleep in
  { 0x2A, 4, { 0x00, RSHIFT, (LCD_FRAME_WIDTH + RSHIFT - 1) >> 8, (LCD_FRAME_WIDTH + RSHIFT - 1) & 0xFF } }, // Column address set
  { 0x2B, 4, { 0x00, 0x00, (LCD_FRAME_HEIGHT -1) >> 8, (LCD_FRAME_HEIGHT -1) & 0xFF } }, // Row address set
  { 0x36, 1, { 0x00 }, 0 }, // Memory data access control
  { 0x3A, 1, { 0x55 }, 0 }, // Interface pixel format (16 bit/pixel 65k RGB data)
  { 0xB0, 2, { 0x00, 0x08 } }, // RAM control (LSB first)
  { 0xB2, 5, { 0x0C, 0x0C, 0x00, 0x33, 0x33 }, 0 }, // Porch setting
  { 0xB7, 1, { 0x72 }, 0 }, // Gate control (VGH 14.97v, VGL -8.23v)
  { 0xBB, 1, { 0x3B }, 0 }, // VCOMS setting (1.575v)
  { 0xC0, 1, { 0x2C }, 0 }, // LCM control
  { 0xC2, 1, { 0x01 }, 0 }, // VDV and VRH command enable
  { 0xC3, 1, { 0x14 }, 0 }, // VRH set
  { 0xC4, 1, { 0x20 }, 0 }, // VDV set
  { 0xC6, 1, { 0x0F }, 0 }, // Frame rate control in normal mode (60hz)
  { 0xD0, 2, { 0xA4, 0xA1 }, 0 }, // Power control 1
  { 0xE0, 14, { 0xD0, 0x10, 0x16, 0x0A, 0x0A, 0x26, 0x3C, 0x53, 0x53, 0x18, 0x15, 0x12, 0x36, 0x3C }, 0 }, // Positive voltage gamma control
  { 0xE1, 14, { 0xD0, 0x11, 0x19, 0x0A, 0x09, 0x25, 0x3D, 0x35, 0x54, 0x17, 0x15, 0x12, 0x36, 0x3C }, 0 }, // Negative voltage gamma control
  { 0xE9, 3, { 0x05, 0x05, 0x01 }, 0 }, // Equalize time control
  { 0x21, 1, { 0x00 }, 0 }, // Display inversion on
  { 0 }
};

static const INIT_SCRIPT display_on_scr[] = {
  { 0x11, 1, { 0x00 }, 120 }, // Sleep out
  { 0x29, 1, { 0x00 }, 120 }, // Display on
  {0}
};

static const INIT_SCRIPT sleep_in[] = {
  { 0x10, 1, { 0x00 }, 5 },
  {0}
};

/************* LCD SPI Interface ***************/


static int lcd_fd;

static int lcd_spi_init()
{
  // SPI setup
  static const uint8_t    MODE = 0;
  int lcd_fd = open("/dev/spidev1.0", O_RDWR);
  if (!lcd_fd) {
    error_exit(app_DEVICE_OPEN_ERROR, "Can't open LCD SPI interface\n");
  }
  ioctl(lcd_fd, SPI_IOC_RD_MODE, &MODE);
  /* if (err<0) { */
  /*   error_exit(app_IO_ERROR, "Can't configure LCD SPI. (%d)\n", errno); */
  /* } */

  // Set MAX_TRANSFER size based on the spidev bufsiz parameter
  int bufsiz_fd = open("/sys/module/spidev/parameters/bufsiz", O_RDONLY);
  if(bufsiz_fd <= 0)
  {
    error_exit(app_DEVICE_OPEN_ERROR, "Can't open SPI bufsiz parameter\n");
  }

  // bufsiz is stored as a string in the file
  char buf[32] = {0};
  int bytes_read = 0;
  
  // Attempt to read enough bytes to fit in our buffer
  while(bytes_read < sizeof(buf))
  {
    int num_bytes = read(bufsiz_fd, buf + bytes_read, sizeof(buf) - bytes_read);
    bytes_read += num_bytes;
    if(num_bytes == 0)
    {
      // End of file
      break;
    }
    else if(num_bytes < 0)
    {
      (void)close(bufsiz_fd);
      error_exit(app_IO_ERROR, "Failed to read from spi bufsiz\n");
    }
  }
  
  char* end;
  int size = strtol(buf, &end, 10);
  printf("LCD.lcd_spi_init.transferSize %d\n", size);
  MAX_TRANSFER = size;

  (void)close(bufsiz_fd);
  
  return lcd_fd;
}

static void lcd_spi_transfer(int cmd, int bytes, const void* data) {
  const uint8_t* tx_buf = data;

  gpio_set_value(DnC_PIN, cmd ? gpio_LOW : gpio_HIGH);

  while (bytes > 0) {
    const size_t count = bytes > MAX_TRANSFER ? MAX_TRANSFER : bytes;

    (void)write(lcd_fd, tx_buf, count);

    bytes -= count;
    tx_buf += count;
  }
}

static void lcd_run_script(const INIT_SCRIPT* script)
{
  int idx;
  for (idx = 0; script[idx].cmd; idx++) {
    lcd_spi_transfer(TRUE, 1, &script[idx].cmd);
    lcd_spi_transfer(FALSE, script[idx].data_bytes, script[idx].data);
    milliwait(script[idx].delay_ms);
  }
}

/************ LCD Framebuffer device *************/
static int lcd_fb_init(void)
{
  struct fb_fix_screeninfo fixed_info;
  int _fd = open("/dev/fb0", O_RDWR | O_NONBLOCK);
  if (_fd <= 0)
    return -1;

  if (ioctl(_fd, FBIOGET_FSCREENINFO, &fixed_info) < 0)
    return -1;

  return _fd;
}

/************ LCD Device Interface *************/

static void lcd_device_init()
{
  // Init registers and put the display in sleep mode
  lcd_run_script(init_scr);

  // Clear lcd memory before turning display on
  // as the contents of memory are set randomly on
  // power on
  lcd_clear_screen();
  // Turn display on
  lcd_run_script(display_on_scr);
}

void lcd_clear_screen(void) {
  const LcdFrame frame={{0}};
  lcd_draw_frame(&frame);
}

void lcd_draw_frame(const LcdFrame* frame) {
   if (lcd_use_fb) {
      lseek(lcd_fd, 0, SEEK_SET);
      (void)write(lcd_fd, frame->data, sizeof(frame->data));
   } else {
      static const uint8_t WRITE_RAM = 0x2C;
      lcd_spi_transfer(TRUE, 1, &WRITE_RAM);
      lcd_spi_transfer(FALSE, sizeof(frame->data), frame->data);
   }
}

void lcd_draw_frame2(const uint16_t* frame, size_t size) {
   if (lcd_use_fb) {
      lseek(lcd_fd, 0, SEEK_SET);
      (void)write(lcd_fd, frame, size);
   } else {
      static const uint8_t WRITE_RAM = 0x2C;
      lcd_spi_transfer(TRUE, 1, &WRITE_RAM);
      lcd_spi_transfer(FALSE, size, frame);
   }
}


#define MAX(a,b) (((a)>(b))?(a):(b))
#define MIN(a,b) (((a)<(b))?(a):(b))

static const char* BACKLIGHT_DEVICES[] = {
  "/sys/class/leds/face-backlight-left/brightness",
  "/sys/class/leds/face-backlight-right/brightness"
};

static void _led_set_brightness(const int brightness, const char* led)
{
  int fd = open(led,O_WRONLY);
  if (fd) {
    char buf[3];
    snprintf(buf,3,"%02d\n",brightness);
    (void)write(fd, buf, 3);
    close(fd);
  }
}

void lcd_set_brightness(int brightness)
{
  int l;
  brightness = MIN(brightness, 20);
  brightness = MAX(brightness, 0);
  for (l=0; l<2; ++l) {
    _led_set_brightness(brightness, BACKLIGHT_DEVICES[l]);
  }
}

int lcd_init(void) {

  lcd_set_brightness(10);

  lcd_fd = lcd_fb_init();
  if (lcd_fd > 0) {
    lcd_use_fb = TRUE;
    return 0; // use framebuffer device
  }

  // IO Setup
  DnC_PIN = gpio_create(GPIO_LCD_WRX, gpio_DIR_OUTPUT, gpio_HIGH);

  RESET_PIN = gpio_create_open_drain_output(GPIO_LCD_RESET, gpio_HIGH);

  // SPI setup

  lcd_fd = lcd_spi_init();

  // Send reset signal
  microwait(50);
  gpio_set_value(RESET_PIN, 0);
  microwait(50);
  gpio_set_value(RESET_PIN, 1);
  // Wait 120 milliseconds after releasing reset before sending commands
  milliwait(120);
  
  lcd_device_init();

  return 0;
}


void lcd_shutdown(void) {
  //todo: turn off screen?

  if (lcd_use_fb) {
    close(lcd_fd);
    return;
  }

  if (lcd_fd) {
    lcd_run_script(sleep_in);
    close(lcd_fd);
  }
  if (DnC_PIN) {
    gpio_close(DnC_PIN);
  }
  if (RESET_PIN) {
    gpio_close(RESET_PIN);
  }

}
