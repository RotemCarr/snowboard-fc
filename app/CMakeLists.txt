cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(snowboard_fc)
target_sources(app PRIVATE src/flight_loop.c)

