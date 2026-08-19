/****************************************************************************
 * drivers/sensors/qmc5883l_uorb.c
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
#include <nuttx/kthread.h>
#include <nuttx/signal.h>
#include <nuttx/fs/fs.h>
#include <nuttx/i2c/i2c_master.h>
#include <nuttx/mutex.h>
#include <nuttx/sensors/qmc5883l.h>
#include <nuttx/sensors/sensor.h>
#include <nuttx/uorb.h>
#include <nuttx/wqueue.h>

#if defined(CONFIG_SENSORS_QMC5883L)

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_QMC5883L_I2C_FREQUENCY
#  define CONFIG_QMC5883L_I2C_FREQUENCY 100000
#endif

#ifndef CONFIG_SENSORS_QMC5883L_POLL_INTERVAL
#  define CONFIG_SENSORS_QMC5883L_POLL_INTERVAL 100000  /* Default 100ms (us) */
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct qmc5883l_dev_s
{
  struct sensor_lowerhalf_s lower;   /* Lower half sensor framework (must be first) */
  FAR struct i2c_master_s *i2c;      /* I2C interface */
  mutex_t lock;                      /* Manages exclusive access */
#ifdef CONFIG_SENSORS_QMC5883L_POLL
  struct work_s work;                /* Polling worker */
  uint32_t interval;                 /* Polling period (us) */
  bool enabled;                      /* Sampling enabled */
#endif
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int qmc5883l_activate(FAR struct sensor_lowerhalf_s *lower,
                             FAR struct file *filep, bool enable);
#ifdef CONFIG_SENSORS_QMC5883L_POLL
static int qmc5883l_set_interval(FAR struct sensor_lowerhalf_s *lower,
                                 FAR struct file *filep,
                                 FAR uint32_t *period_us);
static void qmc5883l_worker(FAR void *arg);
#else
static int qmc5883l_fetch(FAR struct sensor_lowerhalf_s *lower,
                          FAR struct file *filep,
                          FAR char *buffer, size_t buflen);
#endif

static const struct sensor_ops_s g_qmc5883l_ops =
{
  .activate     = qmc5883l_activate,
#ifdef CONFIG_SENSORS_QMC5883L_POLL
  .set_interval = qmc5883l_set_interval,
#else
  .fetch        = qmc5883l_fetch,
#endif
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: qmc5883l_write8
 ****************************************************************************/

static int qmc5883l_write8(FAR struct qmc5883l_dev_s *dev,
                           uint8_t reg, uint8_t val)
{
  struct i2c_config_s config;
  uint8_t buf[2];

  config.frequency = CONFIG_QMC5883L_I2C_FREQUENCY;
  config.address   = QMC5883L_I2C_ADDR;
  config.addrlen   = 7;

  buf[0] = reg;
  buf[1] = val;
  return i2c_write(dev->i2c, &config, buf, 2);
}

/****************************************************************************
 * Name: qmc5883l_readregs
 ****************************************************************************/

static int qmc5883l_readregs(FAR struct qmc5883l_dev_s *dev,
                             uint8_t reg, FAR uint8_t *buffer, size_t len)
{
  struct i2c_config_s config;
  int ret;

  config.frequency = CONFIG_QMC5883L_I2C_FREQUENCY;
  config.address   = QMC5883L_I2C_ADDR;
  config.addrlen   = 7;

  ret = i2c_write(dev->i2c, &config, &reg, 1);
  if (ret < 0)
    {
      return ret;
    }

  return i2c_read(dev->i2c, &config, buffer, len);
}

/****************************************************************************
 * Name: qmc5883l_config
 ****************************************************************************/

static int qmc5883l_config(FAR struct qmc5883l_dev_s *dev)
{
  uint8_t id;
  uint8_t status;
  uint8_t ctrl;
  uint8_t fbr;
  int ret;

  /* Probe WHO_AM_I (0xff on QMC5883L).  Some QMC5883 / clone parts return
   * a different value, so treat the result as informational only and do
   * not refuse to operate the sensor when it mismatches.
   */

  ret = qmc5883l_readregs(dev, QMC5883L_DEVICE_ID, &id, 1);
  if (ret < 0)
    {
      syslog(LOG_ERR, "QMC5883L: read ID failed: %d\n", ret);
    }
  else
    {
      syslog(LOG_INFO, "QMC5883L: chip id: 0x%02x\n", id);
    }

  /* Soft reset, wait, then configure continuous mode, 2G range, 50Hz
   * (flow as used by the LCKFB ESP32-C3 example driver).
   */

  ret = qmc5883l_write8(dev, QMC5883L_CTRL2, QMC5883L_CTRL2_SOFTRST);
  if (ret < 0)
    {
      return ret;
    }

  nxsig_usleep(10000);

  ret = qmc5883l_write8(dev, QMC5883L_CTRL1, QMC5883L_CTRL1_CONT);
  if (ret < 0)
    {
      return ret;
    }

  ret = qmc5883l_write8(dev, QMC5883L_CTRL2, QMC5883L_CTRL2_DEF);
  if (ret < 0)
    {
      return ret;
    }

  ret = qmc5883l_write8(dev, QMC5883L_FBR, QMC5883L_FBR_DEF);
  if (ret < 0)
    {
      return ret;
    }

  /* Diagnostic: read back configuration and status */

  ret = qmc5883l_readregs(dev, QMC5883L_STATUS, &status, 1);
  if (ret >= 0)
    {
      qmc5883l_readregs(dev, QMC5883L_CTRL1, &ctrl, 1);
      qmc5883l_readregs(dev, QMC5883L_FBR, &fbr, 1);
      syslog(LOG_INFO,
             "QMC5883L: status=0x%02x ctrl1=0x%02x fbr=0x%02x\n",
             status, ctrl, fbr);
    }

  return OK;
}

/****************************************************************************
 * Name: qmc5883l_activate
 *
 * Description:
 *   Enable/disable the sensor. On first enable the sensor is configured
 *   (soft reset + continuous mode) and the polling worker is started.
 *
 ****************************************************************************/

static int qmc5883l_activate(FAR struct sensor_lowerhalf_s *lower,
                             FAR struct file *filep, bool enable)
{
  FAR struct qmc5883l_dev_s *dev = (FAR struct qmc5883l_dev_s *)lower;
  int ret = OK;

#ifdef CONFIG_SENSORS_QMC5883L_POLL
  /* Polling mode: (re)configure the chip and start/stop the worker.
   * The worker only runs while sampling is enabled.
   */

  dev->enabled = enable;

  if (enable)
    {
      ret = qmc5883l_config(dev);
      if (ret < 0)
        {
          return ret;
        }

      if (dev->interval > 0)
        {
          uint32_t delay = dev->interval / USEC_PER_TICK;
          ret = work_queue(HPWORK, &dev->work, qmc5883l_worker, dev, delay);
          if (ret < 0)
            {
              snerr("QMC5883L: work_queue failed: %d\n", ret);
            }
        }
    }
  else
    {
      work_cancel(HPWORK, &dev->work);
    }
#else
  /* Fetch mode: configure and wake up the first blocking read() */

  if (enable)
    {
      ret = qmc5883l_config(dev);
      if (ret < 0)
        {
          return ret;
        }

      if (lower->notify_event != NULL && lower->priv != NULL)
        {
          lower->notify_event(lower->priv);
        }
    }
#endif

  return ret;
}

#ifdef CONFIG_SENSORS_QMC5883L_POLL
/****************************************************************************
 * Name: qmc5883l_set_interval
 *
 * Description:
 *   Set the polling interval for sensor data acquisition. The new
 *   interval takes effect at the next worker self-reschedule.
 *
 ****************************************************************************/

static int qmc5883l_set_interval(FAR struct sensor_lowerhalf_s *lower,
                                 FAR struct file *filep,
                                 FAR uint32_t *period_us)
{
  FAR struct qmc5883l_dev_s *dev = (FAR struct qmc5883l_dev_s *)lower;

  if (dev == NULL || period_us == NULL)
    {
      return -EINVAL;
    }

  dev->interval = *period_us;

  return OK;
}

/****************************************************************************
 * Name: qmc5883l_worker
 *
 * Description:
 *   Polling worker. Self-reschedules with the configured interval, checks
 *   the STATUS register DRDY bit and, when new data is available, reads
 *   the full register map and pushes a sensor_mag frame to the framework.
 *   Reading the data registers clears DRDY, so stale frames are skipped
 *   even if the polling interval is shorter than the chip ODR.
 *
 ****************************************************************************/

static void qmc5883l_worker(FAR void *arg)
{
  FAR struct qmc5883l_dev_s *dev = (FAR struct qmc5883l_dev_s *)arg;
  struct sensor_mag mag;
  uint8_t status;
  uint8_t dump[13];
  int16_t x;
  int16_t y;
  int16_t z;
  int16_t t;
  int ret;

  DEBUGASSERT(dev != NULL);

  /* Self-reschedule before sampling so a missed schedule does not stall
   * the sampling chain.
   */

  if (dev->enabled && dev->interval > 0)
    {
      uint32_t delay = dev->interval / USEC_PER_TICK;
      work_queue(HPWORK, &dev->work, qmc5883l_worker, dev, delay);
    }

  nxmutex_lock(&dev->lock);

  /* Check data ready before reading to avoid pushing duplicate frames */

  ret = qmc5883l_readregs(dev, QMC5883L_STATUS, &status, 1);
  if (ret < 0)
    {
      nxmutex_unlock(&dev->lock);
      syslog(LOG_ERR, "QMC5883L: read status failed: %d\n", ret);
      return;
    }

  if (!(status & QMC5883L_STATUS_DRDY))
    {
      nxmutex_unlock(&dev->lock);
      return;
    }

  /* Read the full register map 0x00-0x0c in one shot for diagnosis:
   * [0..5] = X/Y/Z data, [6] = status, [7..8] = temp,
   * [9] = ctrl1, [10] = ctrl2, [11] = ctrl3, [12] = fbr.
   */

  ret = qmc5883l_readregs(dev, 0x00, dump, 13);
  nxmutex_unlock(&dev->lock);
  if (ret < 0)
    {
      syslog(LOG_ERR, "QMC5883L: read data failed: %d\n", ret);
      return;
    }

  x = (int16_t)(dump[0] | (dump[1] << 8));
  y = (int16_t)(dump[2] | (dump[3] << 8));
  z = (int16_t)(dump[4] | (dump[5] << 8));
  t = (int16_t)(dump[7] | (dump[8] << 8));

  mag.timestamp   = sensor_get_timestamp();
  mag.x           = (float)x / QMC5883L_LSB_PER_GAUSS;
  mag.y           = (float)y / QMC5883L_LSB_PER_GAUSS;
  mag.z           = (float)z / QMC5883L_LSB_PER_GAUSS;
  mag.temperature = (float)t / 100.0f;
  mag.status      = dump[6];

  if (dev->lower.push_event != NULL && dev->lower.priv != NULL)
    {
      dev->lower.push_event(dev->lower.priv, &mag, sizeof(mag));
    }
}
#else
/****************************************************************************
 * Name: qmc5883l_fetch
 *
 * Description:
 *   Read magnetometer data and temperature on demand.
 *
 ****************************************************************************/

static int qmc5883l_fetch(FAR struct sensor_lowerhalf_s *lower,
                          FAR struct file *filep,
                          FAR char *buffer, size_t buflen)
{
  FAR struct qmc5883l_dev_s *dev = (FAR struct qmc5883l_dev_s *)lower;
  struct sensor_mag mag;
  uint8_t dump[13];
  int16_t x;
  int16_t y;
  int16_t z;
  int16_t t;
  int ret;

  if (buffer == NULL || buflen < sizeof(mag))
    {
      return -EINVAL;
    }

  nxmutex_lock(&dev->lock);

  /* Read the full register map 0x00-0x0c in one shot for diagnosis:
   * [0..5] = X/Y/Z data, [6] = status, [7..8] = temp,
   * [9] = ctrl1, [10] = ctrl2, [11] = ctrl3, [12] = fbr.
   */

  ret = qmc5883l_readregs(dev, 0x00, dump, 13);
  if (ret < 0)
    {
      nxmutex_unlock(&dev->lock);
      syslog(LOG_ERR, "QMC5883L: read data failed: %d\n", ret);
      return ret;
    }

  nxmutex_unlock(&dev->lock);

  x = (int16_t)(dump[0] | (dump[1] << 8));
  y = (int16_t)(dump[2] | (dump[3] << 8));
  z = (int16_t)(dump[4] | (dump[5] << 8));
  t = (int16_t)(dump[7] | (dump[8] << 8));

  mag.timestamp   = sensor_get_timestamp();
  mag.x           = (float)x / QMC5883L_LSB_PER_GAUSS;
  mag.y           = (float)y / QMC5883L_LSB_PER_GAUSS;
  mag.z           = (float)z / QMC5883L_LSB_PER_GAUSS;
  mag.temperature = (float)t / 100.0f;
  mag.status      = dump[6];

  memcpy(buffer, &mag, sizeof(mag));

  /* Wake up the next blocking read() */

  if (lower->notify_event != NULL && lower->priv != NULL)
    {
      lower->notify_event(lower->priv);
    }

  return sizeof(mag);
}
#endif /* CONFIG_SENSORS_QMC5883L_POLL */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: qmc5883l_register
 ****************************************************************************/

int qmc5883l_register(FAR struct i2c_master_s *i2c, int devno)
{
  FAR struct qmc5883l_dev_s *dev;
  int ret;

  DEBUGASSERT(i2c != NULL);

  dev = kmm_zalloc(sizeof(struct qmc5883l_dev_s));
  if (dev == NULL)
    {
      return -ENOMEM;
    }

  dev->i2c = i2c;
  nxmutex_init(&dev->lock);

#ifdef CONFIG_SENSORS_QMC5883L_POLL
  memset(&dev->work, 0, sizeof(dev->work));
  dev->interval = CONFIG_SENSORS_QMC5883L_POLL_INTERVAL;
  dev->enabled  = false;
#endif

  dev->lower.ops  = &g_qmc5883l_ops;
  dev->lower.type = SENSOR_TYPE_MAGNETIC_FIELD;

  ret = sensor_register(&dev->lower, devno);
  if (ret < 0)
    {
      nxmutex_destroy(&dev->lock);
      kmm_free(dev);
    }

  return ret;
}

#endif /* CONFIG_SENSORS_QMC5883L */
