# ##############################################################################
# nuttx_vendor_espressif/apps/CMakeLists.txt
#
# Out-of-tree custom applications.
#
# This directory is loaded from apps/CMakeLists.txt as:
#   add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../nuttx_vendor_espressif/apps
#                    ${CMAKE_CURRENT_BINARY_DIR}/nuttx_vendor_apps)
#
# It is processed twice by the NuttX build system, like the official apps
# directory itself:
#   - preapps: Kconfig collection only (all CONFIG_* are still empty, so the
#     `if(CONFIG_...)` guards below simply skip building)
#   - apps:    actual build stage, CONFIG_* symbols are already exported
# ##############################################################################

add_subdirectory(apptest)
