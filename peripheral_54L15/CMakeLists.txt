#
# Copyright (c) 2018 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(peripheral_54L15)

target_include_directories(app PRIVATE src)
target_include_directories(app PRIVATE src/ble)
target_include_directories(app PRIVATE src/led)
target_include_directories(app PRIVATE src/lib)
target_include_directories(app PRIVATE src/uarts)
target_include_directories(app PRIVATE src/dw3000)
target_include_directories(app PRIVATE src/dw3000/decadriver)
target_include_directories(app PRIVATE src/dw3000_hw)
target_include_directories(app PRIVATE src/dw3000_node)
# NORDIC SDK APP START
target_sources(app PRIVATE
  src/main.c
  src/led/ws2811_gpio.c
  src/led/ledlight.c
  src/uarts/RS485_deal.c
  src/uarts/zigbee_deal.c
  src/lib/qmath.c
  src/dw3000/deca_rsl.c
  src/dw3000/deca_interface.c
  src/dw3000/decadriver/dw3000_device.c
  src/dw3000_hw/deca_compat.c
  src/dw3000_hw/dw3000_spi.c
  src/dw3000_hw/dw3000_hw.c
  src/dw3000_hw/deca_port.c
  src/dw3000_hw/dw3000_spi_trace.c
  src/dw3000_node/dw3000_node.c
  src/ble/nus_manage.c
  src/ble/peripheral_manage.c
  src/ble/nus_peripheral.c
  src/ble/nus_central_scan.c
  src/ble/central_manage.c
)

# NORDIC SDK APP END
