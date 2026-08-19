/****************************************************************************
 * include/nuttx/sensors/qmc5883l.h
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

#ifndef __INCLUDE_NUTTX_SENSORS_QMC5883L_H
#define __INCLUDE_NUTTX_SENSORS_QMC5883L_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/sensors/ioctl.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define QMC5883L_I2C_ADDR         0x0d

/* Registers (layout as used by the LCKFB ESP32-C3 board driver) */

#define QMC5883L_DATA_X_L         0x00
#define QMC5883L_DATA_X_H         0x01
#define QMC5883L_DATA_Y_L         0x02
#define QMC5883L_DATA_Y_H         0x03
#define QMC5883L_DATA_Z_L         0x04
#define QMC5883L_DATA_Z_H         0x05
#define QMC5883L_STATUS           0x06  /* bit0: DRDY, bit1: OVL */

/* STATUS (0x06) bits */

#define QMC5883L_STATUS_DRDY      0x01  /* data ready (cleared by data read) */
#define QMC5883L_STATUS_OVL       0x02  /* magnetic field overflow */
#define QMC5883L_TEMP_L           0x07
#define QMC5883L_TEMP_H           0x08
#define QMC5883L_CTRL1            0x09
#define QMC5883L_CTRL2            0x0a
#define QMC5883L_FBR              0x0c  /* set/reset period */
#define QMC5883L_DEVICE_ID        0x0d  /* WHO_AM_I, should be 0xff */

/* CTRL1 (0x09):
 *   bit[7:6] OSR : 00=512, 01=256, 10=128, 11=64
 *   bit[5:4] RNG : 00=+/-2G, 01=+/-8G
 *   bit[3:2] ODR : 00=10Hz, 01=50Hz, 10=100Hz, 11=200Hz
 *   bit[1:0] MODE: 00=standby, 01=continuous
 * 0x05 = OSR=512, RNG=2G, ODR=50Hz, continuous (LCKFB example).
 */

#define QMC5883L_CTRL1_CONT       0x05

/* CTRL2 (0x0A): bit7=SOFT_RST, 0x00 = normal operation */

#define QMC5883L_CTRL2_SOFTRST    0x80
#define QMC5883L_CTRL2_DEF        0x00

/* FBR (0x0C): set/reset period, datasheet recommended 0x01 */

#define QMC5883L_FBR_DEF          0x01

/* 2G 量程下 3000 LSB/Gauss */

#define QMC5883L_LSB_PER_GAUSS    3000

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct i2c_master_s; /* Forward reference */

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: qmc5883l_register
 *
 * Description:
 *   Register the QMC5883L magnetometer as a standard NuttX sensor
 *   framework device (/dev/uorb/sensor_mag<devno>).
 *
 * Input Parameters:
 *   i2c   - An instance of the I2C interface to use to communicate with
 *           the QMC5883L
 *   devno - The device number that this device should have.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

int qmc5883l_register(FAR struct i2c_master_s *i2c, int devno);

#endif /* __INCLUDE_NUTTX_SENSORS_QMC5883L_H */
