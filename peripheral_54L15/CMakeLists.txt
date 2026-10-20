#
# Copyright (c) 2018 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(peripheral_54L15)

target_include_directories(app PRIVATE src)
target_include_directories(app PRIVATE src/nfc)
target_include_directories(app PRIVATE src/sensor)
target_include_directories(app PRIVATE src/config)
target_include_directories(app PRIVATE src/led)
target_include_directories(app PRIVATE src/lib)
target_include_directories(app PRIVATE src/uarts)
target_include_directories(app PRIVATE src/dwt/dw3000)
target_include_directories(app PRIVATE src/dwt/dw3000/decadriver)
target_include_directories(app PRIVATE src/dwt/dw3000_hw)
target_include_directories(app PRIVATE src/dwt/dw3000_node)
target_include_directories(app PRIVATE src/dwt/shared_data)
target_include_directories(app PRIVATE src/dwt/platform)
target_include_directories(app PRIVATE src/bms_driver)
target_include_directories(app PRIVATE src/imu_driver)
target_include_directories(app PRIVATE src/power_mgr)
# NORDIC SDK APP START
target_sources(app PRIVATE
  src/main.c
  src/nfc/nfc_config_tag.c
  src/led/ws2811_gpio.c
  src/led/ledlight.c
  src/lib/qmath.c
  src/dwt/dw3000/deca_rsl.c
  src/dwt/dw3000/deca_interface.c
  src/dwt/dw3000/decadriver/dw3000_device.c
  src/dwt/dw3000_hw/deca_compat.c
  src/dwt/dw3000_hw/dw3000_spi.c
  src/dwt/dw3000_hw/dw3000_hw.c
  src/dwt/dw3000_hw/deca_port.c
  src/dwt/dw3000_hw/dw3000_spi_trace.c
  src/dwt/dw3000_node/dw3000_node.c
  src/dwt/platform/config_options.c
  src/dwt/shared_data/shared_functions.c
  src/config/config.c
  src/bms_driver/fuel_gauge.c
  src/imu_driver/bma253_i2c_driver.c
  src/imu_driver/bma253_example.c
  src/buzzer/buzzer.c
  src/power_mgr/power_mgr.c
)

# NORDIC SDK APP END
