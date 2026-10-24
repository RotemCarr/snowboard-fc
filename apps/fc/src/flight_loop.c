#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "fc/arming.h"

LOG_MODULE_REGISTER(flight_loop);

static fc_arming_config_t arming_cfg;
static fc_arming_ctx_t arming_ctx;

int main(void) {
  printk("Hello from: %s\r\n", CONFIG_BOARD_TARGET);

  fc_arming_config_default(&arming_cfg);
  fc_arming_init(&arming_ctx);

  return 0;
}
