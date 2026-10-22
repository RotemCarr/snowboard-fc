#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(flight_loop);

int main(void) {
  printk("Hello from: %s\r\n", CONFIG_BOARD_TARGET);
  return 0;
}
