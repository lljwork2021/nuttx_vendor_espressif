/****************************************************************************
 * include/nuttx/sensors/gxhtc3.h
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

#ifndef __INCLUDE_NUTTX_SENSORS_GXHTC3_H
#define __INCLUDE_NUTTX_SENSORS_GXHTC3_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/sensors/ioctl.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define GXHTC3_I2C_ADDR          0x70

/* Commands (GXHTC3 datasheet) */

#define GXHTC3_CMD_WAKEUP        0x3517  /* Wake up from sleep */
#define GXHTC3_CMD_SINGLE_HP     0x7ca2  /* Single shot, high repeatability */
#define GXHTC3_CMD_READ_ID       0xefc8  /* Read sensor ID */
#define GXHTC3_CMD_SOFT_RESET    0x30a2  /* Soft reset */
#define GXHTC3_CMD_SLEEP         0xb098  /* Enter sleep mode */

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct i2c_master_s; /* Forward reference */

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: gxhtc3_register
 *
 * Description:
 *   Register the GXHTC3 temperature/humidity sensor as standard NuttX
 *   sensor framework devices (/dev/uorb/sensor_ambient_temp<devno> and
 *   /dev/uorb/sensor_humi<devno>).
 *
 * Input Parameters:
 *   i2c   - An instance of the I2C interface to use to communicate with
 *           the GXHTC3
 *   devno - The device number that this device should have.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

int gxhtc3_register(FAR struct i2c_master_s *i2c, int devno);

#endif /* __INCLUDE_NUTTX_SENSORS_GXHTC3_H */
