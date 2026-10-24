#include <fc/arming.h>

/* Default values */
#define FC_ARMING_THROTTLE_LOW_MAX_DEFAULT (0.05f)
#define FC_ARMING_RC_TIMEOUT_US_DEFAULT   (300U * 1000U) 


void fc_arming_config_default(fc_arming_config_t *cfg) {
    *cfg = (fc_arming_config_t) {
        .throttle_low_max = FC_ARMING_THROTTLE_LOW_MAX_DEFAULT,
        .rc_timeout_us = FC_ARMING_RC_TIMEOUT_US_DEFAULT,
    };
}

void fc_arming_init(fc_arming_ctx_t *ctx) {
  *ctx = (fc_arming_ctx_t) {
    .state = FC_ARMING_DISARMED,
    .prev_arm_switch = true,
    .needs_switch_off = true,
  };
}

void fc_arming_step(const fc_arming_config_t *cfg, fc_arming_ctx_t *ctx,
                    const fc_arming_input_t *in, fc_arming_output_t *out) {}