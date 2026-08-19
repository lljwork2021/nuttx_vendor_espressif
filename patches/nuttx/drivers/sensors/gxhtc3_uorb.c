/****************************************************************************
 * drivers/sensors/gxhtc3_uorb.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <debug.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/kmalloc.h>
#include <nuttx/fs/fs.h>
#include <nuttx/i2c/i2c_master.h>
#include <nuttx/mutex.h>
#include <nuttx/signal.h>
#include <nuttx/sensors/gxhtc3.h>
#include <nuttx/sensors/sensor.h>
#include <nuttx/uorb.h>
#include <nuttx/wqueue.h>

#if defined(CONFIG_SENSORS_GXHTC3)

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_GXHTC3_I2C_FREQUENCY
#  define CONFIG_GXHTC3_I2C_FREQUENCY 100000
#endif

#define GXHTC3_LOWER_TEMP  0
#define GXHTC3_LOWER_HUMI  1

#define GXHTC3_MEASURE_DELAY_US  40000  /* Single shot high-precision time */

#ifndef CONFIG_SENSORS_GXHTC3_POLL_INTERVAL
#  define CONFIG_SENSORS_GXHTC3_POLL_INTERVAL 1000000  /* Default 1s (us) */
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct gxhtc3_dev_s; /* Forward reference */

/* Per-device-node container: the sensor framework hands back the lower
 * half pointer it was registered with, so the sensor_lowerhalf_s must be
 * the FIRST member of this struct for the container_of cast to work.
 * This avoids the offsetof trap that breaks the second lower node.
 */

struct gxhtc3_sensor_s
{
  struct sensor_lowerhalf_s lower;   /* Lower half (must be first) */
  FAR struct gxhtc3_dev_s *dev;      /* Back pointer to the parent device */
  uint8_t idx;                       /* GXHTC3_LOWER_TEMP or GXHTC3_LOWER_HUMI */
#ifdef CONFIG_SENSORS_GXHTC3_POLL
  uint32_t interval;                 /* Polling period (us) */
  bool enabled;                      /* Per-node enable flag */
#endif
};

struct gxhtc3_dev_s
{
  FAR struct i2c_master_s *i2c;      /* I2C interface */
  mutex_t lock;                      /* Manages exclusive access */
#ifdef CONFIG_SENSORS_GXHTC3_POLL
  struct work_s work;                /* Shared polling worker: one single
                                      * measurement yields both temp & humi */
#endif
  struct gxhtc3_sensor_s sensor[2];  /* 0: temperature, 1: humidity */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int gxhtc3_activate(FAR struct sensor_lowerhalf_s *lower,
                           FAR struct file *filep, bool enable);
#ifdef CONFIG_SENSORS_GXHTC3_POLL
static int gxhtc3_set_interval(FAR struct sensor_lowerhalf_s *lower,
                               FAR struct file *filep,
                               FAR uint32_t *period_us);
static void gxhtc3_worker(FAR void *arg);
#else
static int gxhtc3_fetch(FAR struct sensor_lowerhalf_s *lower,
                        FAR struct file *filep,
                        FAR char *buffer, size_t buflen);
#endif

static const struct sensor_ops_s g_gxhtc3_ops =
{
  .activate = gxhtc3_activate,
#ifdef CONFIG_SENSORS_GXHTC3_POLL
  .set_interval = gxhtc3_set_interval,
#else
  .fetch    = gxhtc3_fetch,
#endif
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: gxhtc3_crc8
 *
 * Description:
 *   CRC-8 (polynomial 0x31, init 0xff), same as SHT3x.
 *
 ****************************************************************************/

static uint8_t gxhtc3_crc8(FAR const uint8_t *data, size_t len)
{
  uint8_t crc = 0xff;
  size_t i;
  int j;

  for (i = 0; i < len; i++)
    {
      crc ^= data[i];
      for (j = 0; j < 8; j++)
        {
          crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) :
                (uint8_t)(crc << 1);
        }
    }

  return crc;
}

/****************************************************************************
 * Name: gxhtc3_writecmd
 ****************************************************************************/

static int gxhtc3_writecmd(FAR struct gxhtc3_dev_s *dev, uint16_t cmd)
{
  struct i2c_config_s config;
  uint8_t buf[2];

  config.frequency = CONFIG_GXHTC3_I2C_FREQUENCY;
  config.address   = GXHTC3_I2C_ADDR;
  config.addrlen   = 7;

  buf[0] = (uint8_t)(cmd >> 8);
  buf[1] = (uint8_t)(cmd & 0xff);
  return i2c_write(dev->i2c, &config, buf, 2);
}

/****************************************************************************
 * Name: gxhtc3_readdata
 ****************************************************************************/

static int gxhtc3_readdata(FAR struct gxhtc3_dev_s *dev,
                           FAR uint8_t *buffer)
{
  struct i2c_config_s config;

  config.frequency = CONFIG_GXHTC3_I2C_FREQUENCY;
  config.address   = GXHTC3_I2C_ADDR;
  config.addrlen   = 7;

  return i2c_read(dev->i2c, &config, buffer, 6);
}

/****************************************************************************
 * Name: gxhtc3_readid
 *
 * Description:
 *   Read and log the sensor ID register (informational only).
 *
 ****************************************************************************/

static int gxhtc3_readid(FAR struct gxhtc3_dev_s *dev)
{
  struct i2c_config_s config;
  uint8_t buf[3];
  int ret;

  config.frequency = CONFIG_GXHTC3_I2C_FREQUENCY;
  config.address   = GXHTC3_I2C_ADDR;
  config.addrlen   = 7;

  ret = gxhtc3_writecmd(dev, GXHTC3_CMD_READ_ID);
  if (ret < 0)
    {
      return ret;
    }

  ret = i2c_read(dev->i2c, &config, buf, 3);
  if (ret < 0)
    {
      return ret;
    }

  syslog(LOG_INFO, "GXHTC3: sensor id: 0x%02x%02x (crc 0x%02x)\n",
         buf[0], buf[1], buf[2]);
  return OK;
}

/****************************************************************************
 * Name: gxhtc3_measure
 *
 * Description:
 *   Trigger a single shot measurement and return the raw temperature and
 *   humidity values.
 *
 ****************************************************************************/

static int gxhtc3_measure(FAR struct gxhtc3_dev_s *dev,
                          FAR uint16_t *temp, FAR uint16_t *humi)
{
  uint8_t buf[6];
  int ret;

  /* Wake up first: the chip may be in sleep mode and would otherwise
   * ignore the measurement command (it ACKs every byte but never starts
   * a conversion, so the subsequent read is NACKed).
   */

  ret = gxhtc3_writecmd(dev, GXHTC3_CMD_WAKEUP);
  if (ret < 0)
    {
      syslog(LOG_ERR, "GXHTC3: wakeup cmd failed: %d\n", ret);
      return ret;
    }

  nxsig_usleep(1000);

  ret = gxhtc3_writecmd(dev, GXHTC3_CMD_SINGLE_HP);
  if (ret < 0)
    {
      syslog(LOG_ERR, "GXHTC3: measure cmd failed: %d\n", ret);
      return ret;
    }

  nxsig_usleep(GXHTC3_MEASURE_DELAY_US);

  ret = gxhtc3_readdata(dev, buf);
  if (ret < 0)
    {
      syslog(LOG_ERR, "GXHTC3: read data failed: %d\n", ret);
      return ret;
    }

  if (buf[2] != gxhtc3_crc8(buf, 2) || buf[5] != gxhtc3_crc8(buf + 3, 2))
    {
      syslog(LOG_ERR, "GXHTC3: CRC mismatch (raw %02x%02x %02x%02x crc %02x %02x)\n",
             buf[0], buf[1], buf[3], buf[4], buf[2], buf[5]);
      return -EIO;
    }

  *temp = (uint16_t)((buf[0] << 8) | buf[1]);
  *humi = (uint16_t)((buf[3] << 8) | buf[4]);

  return OK;
}

/****************************************************************************
 * Name: gxhtc3_activate
 *
 * Description:
 *   Enable/disable the sensor. On first enable a soft reset is issued and
 *   the pending reader is notified so the first read() returns immediately.
 *
 ****************************************************************************/

static int gxhtc3_activate(FAR struct sensor_lowerhalf_s *lower,
                           FAR struct file *filep, bool enable)
{
  FAR struct gxhtc3_sensor_s *priv =
    (FAR struct gxhtc3_sensor_s *)lower;
  FAR struct gxhtc3_dev_s *dev = priv->dev;
  int ret = OK;

#ifdef CONFIG_SENSORS_GXHTC3_POLL
  /* Polling mode: start/stop the shared worker. The worker keeps running
   * as long as at least one sensor node is enabled.
   */

  priv->enabled = enable;

  if (enable)
    {
      if (priv->interval > 0)
        {
          uint32_t delay = priv->interval / USEC_PER_TICK;
          ret = work_queue(HPWORK, &dev->work, gxhtc3_worker, dev, delay);
          if (ret < 0)
            {
              snerr("GXHTC3: work_queue failed: %d\n", ret);
            }
        }
    }
  else if (!dev->sensor[0].enabled && !dev->sensor[1].enabled)
    {
      work_cancel(HPWORK, &dev->work);
    }
#else
  /* Fetch mode: wake up the blocking read() so it can trigger the first
   * fetch. Without this, read() waits on buffersem forever.
   */

  if (enable)
    {
      ret = gxhtc3_writecmd(dev, GXHTC3_CMD_SOFT_RESET);
      if (ret < 0)
        {
          return ret;
        }

      nxsig_usleep(1000);

      /* Log the sensor ID (informational only) */

      gxhtc3_readid(dev);

      /* Wake up the first blocking read() */

      if (lower->notify_event != NULL && lower->priv != NULL)
        {
          lower->notify_event(lower->priv);
        }
    }
#endif

  return ret;
}

#ifndef CONFIG_SENSORS_GXHTC3_POLL
/****************************************************************************
 * Name: gxhtc3_fetch
 *
 * Description:
 *   Read temperature or humidity on demand.
 *
 ****************************************************************************/

static int gxhtc3_fetch(FAR struct sensor_lowerhalf_s *lower,
                        FAR struct file *filep,
                        FAR char *buffer, size_t buflen)
{
  FAR struct gxhtc3_sensor_s *priv =
    (FAR struct gxhtc3_sensor_s *)lower;
  FAR struct gxhtc3_dev_s *dev = priv->dev;
  int idx = priv->idx;
  uint16_t temp;
  uint16_t humi;
  int ret;

  nxmutex_lock(&dev->lock);

  ret = gxhtc3_measure(dev, &temp, &humi);
  if (ret < 0)
    {
      nxmutex_unlock(&dev->lock);
      return ret;
    }

  nxmutex_unlock(&dev->lock);

  if (idx == GXHTC3_LOWER_TEMP)
    {
      struct sensor_temp stemp;

      if (buffer == NULL || buflen < sizeof(stemp))
        {
          return -EINVAL;
        }

      stemp.timestamp   = sensor_get_timestamp();
      stemp.temperature = -45.0f + 175.0f * temp / 65535.0f;

      memcpy(buffer, &stemp, sizeof(stemp));
      ret = sizeof(stemp);
    }
  else
    {
      struct sensor_humi shumi;

      if (buffer == NULL || buflen < sizeof(shumi))
        {
          return -EINVAL;
        }

      shumi.timestamp = sensor_get_timestamp();
      shumi.humidity  = 100.0f * humi / 65535.0f;

      memcpy(buffer, &shumi, sizeof(shumi));
      ret = sizeof(shumi);
    }

  /* Wake up the next blocking read() */

  if (lower->notify_event != NULL && lower->priv != NULL)
    {
      lower->notify_event(lower->priv);
    }

  return ret;
}
#else
/****************************************************************************
 * Name: gxhtc3_set_interval
 *
 * Description:
 *   Set the polling interval for sensor data acquisition. The new
 *   interval takes effect at the next worker self-reschedule.
 *
 ****************************************************************************/

static int gxhtc3_set_interval(FAR struct sensor_lowerhalf_s *lower,
                               FAR struct file *filep,
                               FAR uint32_t *period_us)
{
  FAR struct gxhtc3_sensor_s *priv =
    (FAR struct gxhtc3_sensor_s *)lower;

  if (priv == NULL || period_us == NULL)
    {
      return -EINVAL;
    }

  priv->interval = *period_us;

  return OK;
}

/****************************************************************************
 * Name: gxhtc3_worker
 *
 * Description:
 *   Polling worker. Both temperature and humidity share a single worker
 *   because one GXHTC3 measurement yields both values at once. The worker
 *   reschedules itself with the smallest interval among the enabled nodes,
 *   then performs one measurement and pushes a frame to each enabled node.
 *
 ****************************************************************************/

static void gxhtc3_worker(FAR void *arg)
{
  FAR struct gxhtc3_dev_s *dev = (FAR struct gxhtc3_dev_s *)arg;
  uint16_t temp;
  uint16_t humi;
  uint32_t interval;
  int ret;

  DEBUGASSERT(dev != NULL);

  /* Self-reschedule with the smallest interval of the enabled nodes */

  interval = UINT32_MAX;
  if (dev->sensor[0].enabled && dev->sensor[0].interval > 0)
    {
      interval = dev->sensor[0].interval;
    }

  if (dev->sensor[1].enabled && dev->sensor[1].interval > 0 &&
      dev->sensor[1].interval < interval)
    {
      interval = dev->sensor[1].interval;
    }

  if (interval != UINT32_MAX)
    {
      uint32_t delay = interval / USEC_PER_TICK;
      work_queue(HPWORK, &dev->work, gxhtc3_worker, dev, delay);
    }

  /* One measurement, two outputs */

  nxmutex_lock(&dev->lock);
  ret = gxhtc3_measure(dev, &temp, &humi);
  nxmutex_unlock(&dev->lock);
  if (ret < 0)
    {
      return;
    }

  if (dev->sensor[0].enabled &&
      dev->sensor[0].lower.push_event != NULL &&
      dev->sensor[0].lower.priv != NULL)
    {
      struct sensor_temp stemp;

      stemp.timestamp   = sensor_get_timestamp();
      stemp.temperature = -45.0f + 175.0f * temp / 65535.0f;

      dev->sensor[0].lower.push_event(dev->sensor[0].lower.priv,
                                      &stemp, sizeof(stemp));
    }

  if (dev->sensor[1].enabled &&
      dev->sensor[1].lower.push_event != NULL &&
      dev->sensor[1].lower.priv != NULL)
    {
      struct sensor_humi shumi;

      shumi.timestamp = sensor_get_timestamp();
      shumi.humidity  = 100.0f * humi / 65535.0f;

      dev->sensor[1].lower.push_event(dev->sensor[1].lower.priv,
                                      &shumi, sizeof(shumi));
    }
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: gxhtc3_register
 ****************************************************************************/

int gxhtc3_register(FAR struct i2c_master_s *i2c, int devno)
{
  FAR struct gxhtc3_dev_s *dev;
  int ret;

  DEBUGASSERT(i2c != NULL);

  dev = kmm_zalloc(sizeof(struct gxhtc3_dev_s));
  if (dev == NULL)
    {
      return -ENOMEM;
    }

  dev->i2c = i2c;
  nxmutex_init(&dev->lock);

#ifdef CONFIG_SENSORS_GXHTC3_POLL
  memset(&dev->work, 0, sizeof(dev->work));
#endif

  dev->sensor[0].lower.ops  = &g_gxhtc3_ops;
  /* 使用 SENSOR_TYPE_TEMPERATURE 使节点为 /dev/uorb/sensor_temp0,
   * 与 uORB 主题 ORB_ID(sensor_temp) 一一对应, 支持 orb_subscribe()
   */

  dev->sensor[0].lower.type = SENSOR_TYPE_TEMPERATURE;
  dev->sensor[0].lower.nbuffer = 2;
  dev->sensor[0].dev        = dev;
  dev->sensor[0].idx        = GXHTC3_LOWER_TEMP;
#ifdef CONFIG_SENSORS_GXHTC3_POLL
  dev->sensor[0].interval   = CONFIG_SENSORS_GXHTC3_POLL_INTERVAL;
  dev->sensor[0].enabled    = false;
#endif

  dev->sensor[1].lower.ops  = &g_gxhtc3_ops;
  dev->sensor[1].lower.type = SENSOR_TYPE_RELATIVE_HUMIDITY;
  dev->sensor[1].lower.nbuffer = 2;
  dev->sensor[1].dev        = dev;
  dev->sensor[1].idx        = GXHTC3_LOWER_HUMI;
#ifdef CONFIG_SENSORS_GXHTC3_POLL
  dev->sensor[1].interval   = CONFIG_SENSORS_GXHTC3_POLL_INTERVAL;
  dev->sensor[1].enabled    = false;
#endif

  ret = sensor_register(&dev->sensor[0].lower, devno);
  if (ret < 0)
    {
      goto errout;
    }

  ret = sensor_register(&dev->sensor[1].lower, devno);
  if (ret < 0)
    {
      sensor_unregister(&dev->sensor[0].lower, devno);
      goto errout;
    }

  return OK;

errout:
  nxmutex_destroy(&dev->lock);
  kmm_free(dev);
  return ret;
}

#endif /* CONFIG_SENSORS_GXHTC3 */
