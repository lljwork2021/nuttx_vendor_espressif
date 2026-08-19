/****************************************************************************
 * boards/esp32c3/esp32c3-devkit/src/esp32c3_ft6336.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>

#include <nuttx/debug.h>
#include <nuttx/i2c/i2c_master.h>
#include <nuttx/input/ft5x06.h>
#include <nuttx/input/touchscreen.h>

#include "espressif/esp_i2c_bitbang.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define FT6336_I2C_ADDRESS   0x38
#define FT6336_I2C_FREQUENCY 400000

#ifndef CONFIG_FT5X06_POLLMODE
#  error "The FT6336 EINT pin is not connected; polling mode is required"
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct ft5x06_config_s g_ft6336_config =
{
  .address   = FT6336_I2C_ADDRESS,
  .frequency = FT6336_I2C_FREQUENCY,
  .lower =
    {
      .xres  = 320,
      .yres  = 240,
      .flags = TOUCH_FLAG_SWAPXY | TOUCH_FLAG_MIRRORY,
    },
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int esp32c3_ft6336_initialize(void)
{
  FAR struct i2c_master_s *i2c;
  int ret;

  i2c = esp_i2cbus_bitbang_initialize();
  if (i2c == NULL)
    {
      i2cerr("ERROR: Failed to initialize FT6336 I2C bus\n");
      return -ENODEV;
    }

  ret = ft5x06_register(i2c, &g_ft6336_config, 0);
  if (ret < 0)
    {
      i2cerr("ERROR: Failed to register FT6336: %d\n", ret);
    }

  return ret;
}
