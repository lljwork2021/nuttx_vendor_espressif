/****************************************************************************
 * boards/esp32c3/esp32c3-devkit/src/esp32c3_st7789.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>

#include <nuttx/board.h>
#include <nuttx/lcd/lcd.h>
#include <nuttx/spi/spi.h>
#include <nuttx/lcd/st7789.h>

#include "espressif/esp_gpio.h"
#include "espressif/esp_spi.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define LCD_SPI_PORT ESPRESSIF_SPI2
#define LCD_DC_PIN   CONFIG_ESPRESSIF_SPI2_MISOPIN
#define LCD_BL_PIN   2

#ifndef CONFIG_SPI_CMDDATA
#  error "The ST7789 driver requires CONFIG_SPI_CMDDATA"
#endif

#ifndef CONFIG_ESPRESSIF_SPI_SWCS
#  error "The ST7789 driver requires CONFIG_ESPRESSIF_SPI_SWCS"
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static FAR struct spi_dev_s *g_spidev;
static FAR struct lcd_dev_s *g_lcd;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int board_lcd_initialize(void)
{
  if (g_spidev != NULL)
    {
      return OK;
    }

  g_spidev = esp_spibus_initialize(LCD_SPI_PORT);
  if (g_spidev == NULL)
    {
      lcderr("ERROR: Failed to initialize SPI port %d\n", LCD_SPI_PORT);
      return -ENODEV;
    }

  /* The display has no MISO signal.  The common ESP32-C3 SPI board logic
   * uses the configured MISO pin as the LCD data/command GPIO.
   */

  esp_configgpio(LCD_DC_PIN, OUTPUT);
  esp_gpiowrite(LCD_DC_PIN, true);

  /* The P-channel backlight switch is active low.  Keep it off until the
   * controller has been initialized and the frame buffer has been cleared.
   */

  esp_configgpio(LCD_BL_PIN, OUTPUT);
  esp_gpiowrite(LCD_BL_PIN, true);

  return OK;
}

FAR struct lcd_dev_s *board_lcd_getdev(int devno)
{
  if (devno != 0 || g_spidev == NULL)
    {
      return NULL;
    }

  if (g_lcd == NULL)
    {
      g_lcd = st7789_lcdinitialize(g_spidev);
      if (g_lcd == NULL)
        {
          lcderr("ERROR: Failed to bind SPI port %d to LCD %d\n",
                 LCD_SPI_PORT, devno);
          return NULL;
        }

      /* GPIO2 low enables the backlight on this board. */

      esp_gpiowrite(LCD_BL_PIN, false);
    }

  return g_lcd;
}

void board_lcd_uninitialize(void)
{
  if (g_lcd != NULL)
    {
      g_lcd->setpower(g_lcd, 0);
      g_lcd = NULL;
    }

  esp_gpiowrite(LCD_BL_PIN, true);

  if (g_spidev != NULL)
    {
      esp_spibus_uninitialize(g_spidev);
      g_spidev = NULL;
    }
}
