#include <fc/arming.h>

/* Default values */
#define FC_ARMING_THROTTLE_LOW_MAX_DEFAULT (0.05f)
#define FC_ARMING_RC_TIMEOUT_US_DEFAULT    (300U * 1000U)

/*
 * Battery defaults, LiPo, per cell. The app should override the totals for the
 * pack actually fitted (later: from the battery monitor's cell detection).
 * Critical is deliberately low: failsafe cuts the motors, so it must only trip
 * on a genuinely failing pack, not on load sag.
 */
#define FC_ARMING_BATTERY_CELLS_DEFAULT (4)    /* 4S */
#define FC_ARMING_CELL_MIN_ARM_V        (3.5f) /* at rest, before takeoff */
#define FC_ARMING_CELL_CRITICAL_V       (3.1f) /* filtered, in flight */

/* -------------------------- Helper functions -------------------------- */
static bool is_rc_link_valid(const fc_arming_config_t *cfg, const fc_arming_input_t *in)
{
	if (in->rc_last_frame_us != 0U) {
		return in->now_us - in->rc_last_frame_us < cfg->rc_timeout_us;
	}

	return false;
}

static bool is_throttle_low(const fc_arming_config_t *cfg, const fc_arming_input_t *in)
{
	return (in->throttle >= 0.0f) && (in->throttle <= cfg->throttle_low_max);
}

static bool is_battery_sufficient_for_arming(const fc_arming_config_t *cfg,
					     const fc_arming_input_t *in)
{
	return in->battery_voltage_v >= cfg->battery_min_arm_v;
}

static bool is_battery_voltage_critical(const fc_arming_config_t *cfg, const fc_arming_input_t *in)
{
	return in->battery_voltage_v < cfg->battery_critical_v;
}

static uint32_t compute_block_reasons(const fc_arming_config_t *cfg, const fc_arming_input_t *in,
				      const fc_arming_ctx_t *ctx)
{
	uint32_t reasons = 0U;

	if (!is_throttle_low(cfg, in)) {
		reasons |= FC_ARMING_BLOCK_THROTTLE_HIGH;
	}

	if (!is_rc_link_valid(cfg, in)) {
		reasons |= FC_ARMING_BLOCK_NO_RC_LINK;
	}

	if (!in->imu_healthy) {
		reasons |= FC_ARMING_BLOCK_IMU_UNHEALTHY;
	}

	if (!in->gyro_calibrated) {
		reasons |= FC_ARMING_BLOCK_GYRO_NOT_CAL;
	}

	if (!is_battery_sufficient_for_arming(cfg, in)) {
		reasons |= FC_ARMING_BLOCK_BATTERY_LOW;
	}

	if (ctx->needs_switch_off) {
		reasons |= FC_ARMING_BLOCK_SWITCH_NOT_CYCLED;
	}

	return reasons;
}

static uint32_t compute_failsafe_reasons(const fc_arming_config_t *cfg, const fc_arming_input_t *in)
{
	uint32_t reasons = 0U;

	if (!is_rc_link_valid(cfg, in)) {
		reasons |= FC_ARMING_FAILSAFE_RC_LOST;
	}

	if (!in->imu_healthy) {
		reasons |= FC_ARMING_FAILSAFE_IMU_FAULT;
	}

	if (is_battery_voltage_critical(cfg, in)) {
		reasons |= FC_ARMING_FAILSAFE_BATTERY_CRITICAL;
	}

	return reasons;
}

/* ---------------------------------------------------------------------- */

void fc_arming_config_default(fc_arming_config_t *cfg)
{
	*cfg = (fc_arming_config_t){
		.throttle_low_max = FC_ARMING_THROTTLE_LOW_MAX_DEFAULT,
		.rc_timeout_us = FC_ARMING_RC_TIMEOUT_US_DEFAULT,
		.battery_min_arm_v = FC_ARMING_CELL_MIN_ARM_V * FC_ARMING_BATTERY_CELLS_DEFAULT,
		.battery_critical_v = FC_ARMING_CELL_CRITICAL_V * FC_ARMING_BATTERY_CELLS_DEFAULT,
	};
}

void fc_arming_init(fc_arming_ctx_t *ctx)
{
	*ctx = (fc_arming_ctx_t){
		.state = FC_ARMING_DISARMED,
		.prev_arm_switch = true,
		.needs_switch_off = true,
	};
}

void fc_arming_step(const fc_arming_config_t *cfg, fc_arming_ctx_t *ctx,
		    const fc_arming_input_t *in, fc_arming_output_t *out)
{
	/* Start from safe outputs: disarmed, motors off */
	*out = (fc_arming_output_t){0};

	/* Update the switch latch */
	if (!in->arm_switch) {
		ctx->needs_switch_off = false;
	}

	/* Compute the reasons arming is blocked, 0 if unblocked */
	uint32_t block_reasons = compute_block_reasons(cfg, in, ctx);

	/* Rising edge: switch on now, off last tick */
	bool rising_edge = in->arm_switch && !ctx->prev_arm_switch;

	/* Make state transition if allowed */
	switch (ctx->state) {
	case FC_ARMING_DISARMED: {
		if (rising_edge && block_reasons == 0U) {
			ctx->state = FC_ARMING_ARMED;
			out->events |= FC_ARMING_EVENT_ARMED;
		}
		break;
	}

	case FC_ARMING_ARMED: {
		if (!in->arm_switch) {
			ctx->state = FC_ARMING_DISARMED;
			out->events |= FC_ARMING_EVENT_DISARMED;
			break;
		}

		uint32_t failsafe_reasons = compute_failsafe_reasons(cfg, in);

		if (failsafe_reasons != 0U) {
			ctx->state = FC_ARMING_FAILSAFE;
			ctx->failsafe_reasons = failsafe_reasons;
			out->events |= FC_ARMING_EVENT_FAILSAFE;
		}
		break;
	}

	case FC_ARMING_FAILSAFE: {
		if (is_rc_link_valid(cfg, in) && !in->arm_switch) {
			ctx->state = FC_ARMING_DISARMED;
			ctx->failsafe_reasons = 0U;
			out->events |= FC_ARMING_EVENT_DISARMED;
		}
		break;
	}

	default: {
		ctx->state = FC_ARMING_DISARMED;
		ctx->failsafe_reasons = 0U;
		out->events |= FC_ARMING_EVENT_DISARMED;
		break;
	}
	}

	/* Store last arm state in context */
	ctx->prev_arm_switch = in->arm_switch;

	/* Store current state context in output */
	out->state = ctx->state;

	/* If armed, motors are enabled */
	out->motors_enabled = (ctx->state == FC_ARMING_ARMED);

	/* Fill blocks and failsafe reasons */
	out->block_reasons = block_reasons;
	out->failsafe_reasons = ctx->failsafe_reasons;
}
