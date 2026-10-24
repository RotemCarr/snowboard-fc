# Snowboard flight logic: pure C, no Zephyr includes (RFC 0002 §2.3).
zephyr_library_named(fc)

zephyr_library_sources(
  src/arming.c
)

# Public headers: callers use #include <fc/arming.h>
zephyr_include_directories(include)