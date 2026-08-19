# ##############################################################################
# nuttx_vendor_espressif/apps/apptest/CMakeLists.txt
#
# Real-time sensor monitor GUI (LVGL) for ESP32-C3 devkit
#
# SPDX-License-Identifier: Apache-2.0
# ##############################################################################

if(CONFIG_APPTEST)
  nuttx_add_application(
    NAME
    apptest
    PRIORITY
    ${CONFIG_APPTEST_PRIORITY}
    STACKSIZE
    ${CONFIG_APPTEST_STACKSIZE}
    MODULE
    ${CONFIG_APPTEST}
    DEPENDS
    lvgl
    SRCS
    apptest_main.c)
endif()
