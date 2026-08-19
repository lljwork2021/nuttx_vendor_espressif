/****************************************************************************
 * nuttx_vendor_espressif/apps/apptest/apptest_main.c
 *
 * Real-time sensor monitor GUI (LVGL) for the ESP32-C3 devkit.
 *
 * Displays data from:
 *   - QMI8658  accelerometer + gyroscope
 *   - QMC5883L magnetometer
 *   - GXHTC3   temperature + humidity
 *
 * Data is consumed from the NuttX uORB sensor character devices
 * (/dev/uorb/sensor_*) opened in non-blocking mode.  A periodic LVGL
 * timer drains the pending events and updates the on-screen labels.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

#include <nuttx/sensors/sensor.h>
#include <nuttx/sensors/ioctl.h>

#include <lvgl/lvgl.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define APPTEST_INTERVAL_US   (100000u)   /* sensor sampling: 100 ms */
#define APPTEST_REFRESH_MS    (100)       /* UI refresh period     */
#define APPTEST_BUF_SIZE      (64)        /* >= largest sensor struct */

/****************************************************************************
 * Private Types
 ****************************************************************************/

enum
{
  CH_ACCEL = 0,
  CH_GYRO,
  CH_MAG,
  CH_TEMP,
  CH_HUMI,
  NUM_CH
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const FAR char * const g_path[NUM_CH] =
{
  "/dev/uorb/sensor_accel0",
  "/dev/uorb/sensor_gyro0",
  "/dev/uorb/sensor_mag0",
  "/dev/uorb/sensor_temp0",
  "/dev/uorb/sensor_humi0",
};

static int g_fd[NUM_CH] =
{
  -1, -1, -1, -1, -1
};

static lv_obj_t *g_label[NUM_CH];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: open_sensors
 *
 * Description:
 *   Open all sensor devices non-blocking and set the sampling interval.
 ****************************************************************************/

static void open_sensors(void)
{
  int i;

  for (i = 0; i < NUM_CH; i++)
    {
      int fd = open(g_path[i], O_RDONLY | O_NONBLOCK);
      if (fd < 0)
        {
          printf("apptest: open %s failed: %d\n", g_path[i], errno);
          continue;
        }

      /* SNIOC_SET_INTERVAL: arg is the interval value in microseconds */

      ioctl(fd, SNIOC_SET_INTERVAL, APPTEST_INTERVAL_US);

      g_fd[i] = fd;
    }
}

/****************************************************************************
 * Name: update_label
 ****************************************************************************/

static void update_label(int idx, FAR const char *fmt, ...)
{
  char text[APPTEST_BUF_SIZE];
  va_list ap;

  va_start(ap, fmt);
  vsnprintf(text, sizeof(text), fmt, ap);
  va_end(ap);

  lv_label_set_text(g_label[idx], text);
}

/****************************************************************************
 * Name: read_all_sensors
 *
 * Description:
 *   Drain pending events from every sensor and refresh the UI labels.
 ****************************************************************************/

static void read_all_sensors(void)
{
  /* 8-byte aligned buffer so the sensor structs can be cast safely */

  static uint64_t buf[APPTEST_BUF_SIZE / sizeof(uint64_t)];
  int i;

  for (i = 0; i < NUM_CH; i++)
    {
      ssize_t nread;
      bool got = false;

      if (g_fd[i] < 0)
        {
          continue;
        }

      /* Drain all pending events, keep the latest one */

      do
        {
          nread = read(g_fd[i], buf, sizeof(buf));
          if (nread > 0)
            {
              got = true;
            }
        }
      while (nread > 0);

      if (!got)
        {
          continue;
        }

      switch (i)
        {
          case CH_ACCEL:
            {
              FAR struct sensor_accel *e =
                  (FAR struct sensor_accel *)buf;

              update_label(i, "ACC X%7.2f Y%7.2f Z%7.2f m/s2",
                           e->x, e->y, e->z);
              break;
            }

          case CH_GYRO:
            {
              FAR struct sensor_gyro *e =
                  (FAR struct sensor_gyro *)buf;

              update_label(i, "GYR X%7.2f Y%7.2f Z%7.2f rad/s",
                           e->x, e->y, e->z);
              break;
            }

          case CH_MAG:
            {
              FAR struct sensor_mag *e =
                  (FAR struct sensor_mag *)buf;

              update_label(i, "MAG X%7.2f Y%7.2f Z%7.2f G",
                           e->x, e->y, e->z);
              break;
            }

          case CH_TEMP:
            {
              FAR struct sensor_temp *e =
                  (FAR struct sensor_temp *)buf;

              update_label(i, "TMP %6.2f C", e->temperature);
              break;
            }

          case CH_HUMI:
            {
              FAR struct sensor_humi *e =
                  (FAR struct sensor_humi *)buf;

              update_label(i, "HUM %5.2f %%", e->humidity);
              break;
            }
        }
    }
}

/****************************************************************************
 * Name: sensor_timer_cb
 ****************************************************************************/

static void sensor_timer_cb(lv_timer_t *timer)
{
  read_all_sensors();
}

/****************************************************************************
 * Name: create_ui
 ****************************************************************************/

static void create_ui(void)
{
  static const uint32_t colors[NUM_CH] =
  {
    0x66ccff,   /* accel: light blue  */
    0x99ff99,   /* gyro:  green       */
    0xffb366,   /* mag:   orange      */
    0xff9966,   /* temp:  orange-red  */
    0x66ffcc    /* humi:  aqua        */
  };

  lv_obj_t *scr;
  lv_obj_t *title;
  int i;

  scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  title = lv_label_create(scr);
  lv_label_set_text(title, "ESP32-C3 Sensor Monitor");
  lv_obj_set_style_text_color(title, lv_color_hex(0xf5c542), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

  for (i = 0; i < NUM_CH; i++)
    {
      g_label[i] = lv_label_create(scr);
      lv_label_set_text(g_label[i], "--");
      lv_obj_set_style_text_color(g_label[i], lv_color_hex(colors[i]), 0);
      lv_obj_align(g_label[i], LV_ALIGN_TOP_LEFT, 8, 28 + i * 21);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  lv_nuttx_dsc_t info;
  lv_nuttx_result_t result;

  if (lv_is_initialized())
    {
      LV_LOG_ERROR("apptest: LVGL already initialized, aborting");
      return -1;
    }

  lv_init();

  lv_nuttx_dsc_init(&info);

#ifdef CONFIG_LV_USE_NUTTX_LCD
  info.fb_path = "/dev/lcd0";
#endif

#ifdef CONFIG_INPUT_TOUCHSCREEN
  info.input_path = "/dev/input0";
#endif

  lv_nuttx_init(&info, &result);

  if (result.disp == NULL)
    {
      LV_LOG_ERROR("apptest: display init failure");
      return 1;
    }

  open_sensors();
  create_ui();
  lv_timer_create(sensor_timer_cb, APPTEST_REFRESH_MS, NULL);

  while (1)
    {
      uint32_t idle = lv_timer_handler();

      /* Minimum sleep of 1 ms */

      usleep((idle ? idle : 1) * 1000);
    }

  return 0;
}
