/*
 * Tests for lib/fc arming state machine (fc/arming.h).
 *
 * Written test-first: they encode the rule table in fc/arming.h plus the
 * following decisions, which the header comment should also state:
 *
 *  D1  rc_last_frame_us == 0 means "no frame ever received" (link invalid).
 *  D2  Link is valid while (now - last_frame) < rc_timeout_us. An age equal to
 *      the timeout is lost. A last-frame time later than now is invalid.
 *  D3  Throttle <= throttle_low_max counts as low. NaN, negative and > 1.0
 *      count as high.
 *  D4  Battery >= battery_min_arm_v is OK to arm. Battery < battery_critical_v
 *      is critical; equal is not critical. NaN blocks arming.
 *  D5  Armed + arm switch off + failsafe condition on the same tick:
 *      disarm wins (DISARMED, no FAILSAFE event).
 *  D6  FAILSAFE -> DISARMED raises FC_ARMING_EVENT_DISARMED and clears the
 *      latched failsafe reasons.
 *  D7  Block reasons are computed every step in every state, but only gate
 *      DISARMED -> ARMED. They never disarm.
 *
 * Tests set an explicit config instead of using the defaults, so they don't
 * depend on default values. The defaults have their own sanity tests.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

#include <fc/arming.h>

/* 1 s: non-zero, so 0 can keep meaning "never received" (D1). */
#define T0_US   (1000000ULL)
/* 1 kHz control loop. */
#define TICK_US (1000ULL)

#define TEST_THROTTLE_LOW_MAX  (0.05f)
#define TEST_RC_TIMEOUT_US     (300U * 1000U)
/* Test values only (4S pack at 3.5 V and 3.3 V per cell). */
#define TEST_BATT_MIN_ARM_V    (14.0f)
#define TEST_BATT_CRITICAL_V   (13.2f)
#define TEST_BATT_HEALTHY_V    (16.0f)

#define ALL_BLOCK_BITS                                                         \
	(FC_ARMING_BLOCK_THROTTLE_HIGH | FC_ARMING_BLOCK_NO_RC_LINK |              \
	 FC_ARMING_BLOCK_IMU_UNHEALTHY | FC_ARMING_BLOCK_GYRO_NOT_CAL |            \
	 FC_ARMING_BLOCK_BATTERY_LOW | FC_ARMING_BLOCK_SWITCH_NOT_CYCLED)

#define ALL_FAILSAFE_BITS                                                      \
	(FC_ARMING_FAILSAFE_RC_LOST | FC_ARMING_FAILSAFE_IMU_FAULT |               \
	 FC_ARMING_FAILSAFE_BATTERY_CRITICAL)

#define ALL_EVENT_BITS                                                         \
	(FC_ARMING_EVENT_ARMED | FC_ARMING_EVENT_DISARMED | FC_ARMING_EVENT_FAILSAFE)

static fc_arming_config_t cfg;
static fc_arming_ctx_t ctx;
static fc_arming_input_t in;
static fc_arming_output_t out;

/* ------------------------------------------------------------------------- */
/* Harness                                                                    */
/* ------------------------------------------------------------------------- */

/* Properties that must hold after every single step. */
static void check_invariants(void)
{
	zassert_equal(out.motors_enabled, out.state == FC_ARMING_ARMED,
		      "motors_enabled must be true exactly when ARMED");
	zassert_equal(out.failsafe_reasons != 0U, out.state == FC_ARMING_FAILSAFE,
		      "failsafe reasons must be set exactly when in FAILSAFE");
	zassert_equal(out.block_reasons & ~ALL_BLOCK_BITS, 0U, "unknown block bit");
	zassert_equal(out.failsafe_reasons & ~ALL_FAILSAFE_BITS, 0U,
		      "unknown failsafe bit");
	zassert_equal(out.events & ~ALL_EVENT_BITS, 0U, "unknown event bit");
}

/* One step at the current time, inputs as they are. */
static void step(void)
{
	fc_arming_step(&cfg, &ctx, &in, &out);
	check_invariants();
}

/* Advance one loop tick with a healthy link (a fresh RC frame this tick). */
static void tick(void)
{
	in.now_us += TICK_US;
	in.rc_last_frame_us = in.now_us;
	step();
}

/* Advance one loop tick without receiving an RC frame. */
static void tick_no_rc(void)
{
	in.now_us += TICK_US;
	step();
}

/* Make the last RC frame exactly age_us old, then step. */
static void step_with_rc_age(uint64_t age_us)
{
	in.rc_last_frame_us = in.now_us - age_us;
	step();
}

/* Cycle the switch off then on with healthy inputs and require ARMED. */
static void arm(void)
{
	in.arm_switch = false;
	tick();
	in.arm_switch = true;
	tick();
	zassert_equal(out.state, FC_ARMING_ARMED, "precondition: failed to arm");
}

/* Arm, then lose the RC link until failsafe. */
static void enter_failsafe_rc_lost(void)
{
	arm();
	step_with_rc_age(TEST_RC_TIMEOUT_US);
	zassert_equal(out.state, FC_ARMING_FAILSAFE, "precondition: no failsafe");
}

static void before_each(void *fixture)
{
	ARG_UNUSED(fixture);

	cfg = (fc_arming_config_t){
		.throttle_low_max = TEST_THROTTLE_LOW_MAX,
		.rc_timeout_us = TEST_RC_TIMEOUT_US,
		.battery_min_arm_v = TEST_BATT_MIN_ARM_V,
		.battery_critical_v = TEST_BATT_CRITICAL_V,
	};

	fc_arming_init(&ctx);

	in = (fc_arming_input_t){
		.now_us = T0_US,
		.rc_last_frame_us = T0_US,
		.arm_switch = false,
		.throttle = 0.0f,
		.imu_healthy = true,
		.gyro_calibrated = true,
		.battery_voltage_v = TEST_BATT_HEALTHY_V,
	};

	memset(&out, 0, sizeof(out));
}

ZTEST_SUITE(fc_arming, NULL, NULL, before_each, NULL, NULL);

/* ------------------------------------------------------------------------- */
/* Defaults                                                                   */
/* ------------------------------------------------------------------------- */

ZTEST(fc_arming, test_defaults_throttle_low_is_small_and_positive)
{
	fc_arming_config_t d;

	fc_arming_config_default(&d);
	zassert_true(d.throttle_low_max > 0.0f);
	zassert_true(d.throttle_low_max < 0.2f);
}

/* Catches a microsecond/millisecond slip in either direction. */
ZTEST(fc_arming, test_defaults_rc_timeout_is_plausible)
{
	fc_arming_config_t d;

	fc_arming_config_default(&d);
	zassert_true(d.rc_timeout_us >= 10U * 1000U, "timeout below 10 ms");
	zassert_true(d.rc_timeout_us <= 2U * 1000U * 1000U, "timeout above 2 s");
}

/* Zero thresholds would silently disable both battery checks. */
ZTEST(fc_arming, test_defaults_battery_thresholds_set_and_ordered)
{
	fc_arming_config_t d;

	fc_arming_config_default(&d);
	zassert_true(d.battery_critical_v > 0.0f, "critical threshold unset");
	zassert_true(d.battery_min_arm_v > d.battery_critical_v,
		     "arm minimum must be above critical");
}

/* ------------------------------------------------------------------------- */
/* Init and boot                                                              */
/* ------------------------------------------------------------------------- */

ZTEST(fc_arming, test_boot_with_switch_off_is_disarmed_and_quiet)
{
	step();
	zassert_equal(out.state, FC_ARMING_DISARMED);
	zassert_false(out.motors_enabled);
	zassert_equal(out.events, 0U);
	zassert_equal(out.block_reasons, 0U);
}

ZTEST(fc_arming, test_boot_with_switch_on_does_not_arm)
{
	in.arm_switch = true;
	step();
	zassert_equal(out.state, FC_ARMING_DISARMED);
	zassert_equal(out.block_reasons, FC_ARMING_BLOCK_SWITCH_NOT_CYCLED);
	zassert_equal(out.events, 0U);

	for (int i = 0; i < 100; i++) {
		tick();
		zassert_equal(out.state, FC_ARMING_DISARMED, "armed while held on");
	}
}

ZTEST(fc_arming, test_boot_with_switch_on_arms_after_cycle)
{
	in.arm_switch = true;
	tick();
	zassert_equal(out.state, FC_ARMING_DISARMED);

	in.arm_switch = false;
	tick();
	zassert_equal(out.block_reasons & FC_ARMING_BLOCK_SWITCH_NOT_CYCLED, 0U,
		      "switch off must clear the not-cycled block");

	in.arm_switch = true;
	tick();
	zassert_equal(out.state, FC_ARMING_ARMED);
}

/* ------------------------------------------------------------------------- */
/* Arming                                                                     */
/* ------------------------------------------------------------------------- */

ZTEST(fc_arming, test_arms_on_rising_edge_when_healthy)
{
	in.arm_switch = false;
	tick();
	in.arm_switch = true;
	tick();

	zassert_equal(out.state, FC_ARMING_ARMED);
	zassert_true(out.motors_enabled);
	zassert_equal(out.events, FC_ARMING_EVENT_ARMED);
	zassert_equal(out.block_reasons, 0U);
}

ZTEST(fc_arming, test_armed_event_is_one_shot)
{
	arm();
	tick();
	zassert_equal(out.state, FC_ARMING_ARMED);
	zassert_equal(out.events, 0U);
}

/* A block that clears while the switch is held on must not arm (D7). */
ZTEST(fc_arming, test_block_clearing_while_switch_held_does_not_arm)
{
	in.arm_switch = false;
	tick();

	in.throttle = 0.5f;
	in.arm_switch = true;
	tick();
	zassert_equal(out.state, FC_ARMING_DISARMED);

	in.throttle = 0.0f;
	for (int i = 0; i < 100; i++) {
		tick();
		zassert_equal(out.state, FC_ARMING_DISARMED,
			      "armed without a fresh switch edge");
	}

	in.arm_switch = false;
	tick();
	in.arm_switch = true;
	tick();
	zassert_equal(out.state, FC_ARMING_ARMED);
}

/* Each condition alone: blocks arming and sets exactly its own bit. */
static void expect_single_block(uint32_t bit)
{
	in.arm_switch = false;
	step();
	zassert_equal(out.block_reasons, bit, "wrong block bits: 0x%x",
		      (unsigned int)out.block_reasons);

	in.arm_switch = true;
	step();
	zassert_equal(out.state, FC_ARMING_DISARMED, "armed despite block");
	zassert_equal(out.block_reasons, bit, "wrong block bits: 0x%x",
		      (unsigned int)out.block_reasons);
}

/* Clear the boot latch with a healthy tick before testing one condition. */
static void clear_boot_latch(void)
{
	in.arm_switch = false;
	tick();
}

ZTEST(fc_arming, test_throttle_high_blocks)
{
	clear_boot_latch();
	in.throttle = 0.3f;
	expect_single_block(FC_ARMING_BLOCK_THROTTLE_HIGH);
}

ZTEST(fc_arming, test_throttle_just_above_threshold_blocks)
{
	clear_boot_latch();
	in.throttle = TEST_THROTTLE_LOW_MAX + 0.001f;
	expect_single_block(FC_ARMING_BLOCK_THROTTLE_HIGH);
}

ZTEST(fc_arming, test_throttle_at_threshold_counts_as_low)
{
	in.throttle = TEST_THROTTLE_LOW_MAX;
	arm();
}

ZTEST(fc_arming, test_throttle_nan_blocks)
{
	clear_boot_latch();
	in.throttle = NAN;
	expect_single_block(FC_ARMING_BLOCK_THROTTLE_HIGH);
}

ZTEST(fc_arming, test_throttle_negative_blocks)
{
	clear_boot_latch();
	in.throttle = -0.1f;
	expect_single_block(FC_ARMING_BLOCK_THROTTLE_HIGH);
}

ZTEST(fc_arming, test_throttle_above_one_blocks)
{
	clear_boot_latch();
	in.throttle = 1.5f;
	expect_single_block(FC_ARMING_BLOCK_THROTTLE_HIGH);
}

ZTEST(fc_arming, test_rc_link_timed_out_blocks)
{
	clear_boot_latch();
	in.rc_last_frame_us = in.now_us - TEST_RC_TIMEOUT_US - 1U;
	expect_single_block(FC_ARMING_BLOCK_NO_RC_LINK);
}

ZTEST(fc_arming, test_rc_age_equal_to_timeout_is_lost)
{
	clear_boot_latch();
	in.rc_last_frame_us = in.now_us - TEST_RC_TIMEOUT_US;
	expect_single_block(FC_ARMING_BLOCK_NO_RC_LINK);
}

ZTEST(fc_arming, test_rc_age_just_below_timeout_is_valid)
{
	clear_boot_latch();
	in.rc_last_frame_us = in.now_us - (TEST_RC_TIMEOUT_US - 1U);
	step();
	zassert_equal(out.block_reasons, 0U);
}

ZTEST(fc_arming, test_rc_never_received_blocks)
{
	clear_boot_latch();
	in.rc_last_frame_us = 0U;
	expect_single_block(FC_ARMING_BLOCK_NO_RC_LINK);
}

/* Early boot: now is still inside the first timeout window (D1). */
ZTEST(fc_arming, test_rc_never_received_blocks_right_after_boot)
{
	in.now_us = TEST_RC_TIMEOUT_US / 2U;
	in.rc_last_frame_us = 0U;
	expect_single_block(FC_ARMING_BLOCK_NO_RC_LINK);
}

ZTEST(fc_arming, test_rc_frame_in_future_blocks)
{
	clear_boot_latch();
	in.rc_last_frame_us = in.now_us + 1U;
	expect_single_block(FC_ARMING_BLOCK_NO_RC_LINK);
}

/* A 32-bit truncation of the age would wrap to 1 ms and look valid. */
ZTEST(fc_arming, test_rc_age_beyond_32bit_range_is_lost)
{
	clear_boot_latch();
	in.now_us = in.rc_last_frame_us + (1ULL << 32) + 1000U;
	expect_single_block(FC_ARMING_BLOCK_NO_RC_LINK);
}

ZTEST(fc_arming, test_imu_unhealthy_blocks)
{
	clear_boot_latch();
	in.imu_healthy = false;
	expect_single_block(FC_ARMING_BLOCK_IMU_UNHEALTHY);
}

ZTEST(fc_arming, test_gyro_not_calibrated_blocks)
{
	clear_boot_latch();
	in.gyro_calibrated = false;
	expect_single_block(FC_ARMING_BLOCK_GYRO_NOT_CAL);
}

ZTEST(fc_arming, test_battery_low_blocks)
{
	clear_boot_latch();
	in.battery_voltage_v = TEST_BATT_MIN_ARM_V - 0.1f;
	expect_single_block(FC_ARMING_BLOCK_BATTERY_LOW);
}

ZTEST(fc_arming, test_battery_at_min_arm_threshold_arms)
{
	in.battery_voltage_v = TEST_BATT_MIN_ARM_V;
	arm();
}

ZTEST(fc_arming, test_battery_nan_blocks)
{
	clear_boot_latch();
	in.battery_voltage_v = NAN;
	expect_single_block(FC_ARMING_BLOCK_BATTERY_LOW);
}

/* USB-only bench power reads near zero: arming must be refused. */
ZTEST(fc_arming, test_battery_zero_blocks)
{
	clear_boot_latch();
	in.battery_voltage_v = 0.0f;
	expect_single_block(FC_ARMING_BLOCK_BATTERY_LOW);
}

ZTEST(fc_arming, test_multiple_blocks_all_reported)
{
	clear_boot_latch();
	in.throttle = 0.5f;
	in.imu_healthy = false;
	in.battery_voltage_v = 0.0f;
	expect_single_block(FC_ARMING_BLOCK_THROTTLE_HIGH |
			    FC_ARMING_BLOCK_IMU_UNHEALTHY |
			    FC_ARMING_BLOCK_BATTERY_LOW);
}

ZTEST(fc_arming, test_works_with_timestamps_past_32bit_range)
{
	in.now_us = (1ULL << 33) + 12345U;
	in.rc_last_frame_us = in.now_us;
	arm();

	step_with_rc_age(TEST_RC_TIMEOUT_US - 1U);
	zassert_equal(out.state, FC_ARMING_ARMED);

	step_with_rc_age(TEST_RC_TIMEOUT_US);
	zassert_equal(out.state, FC_ARMING_FAILSAFE);
}

/* ------------------------------------------------------------------------- */
/* While armed                                                                */
/* ------------------------------------------------------------------------- */

/* Non-failsafe conditions are reported but never disarm (D7). */
ZTEST(fc_arming, test_blocks_while_armed_are_reported_but_do_not_disarm)
{
	arm();

	in.throttle = 1.0f;
	in.gyro_calibrated = false;
	in.battery_voltage_v = TEST_BATT_MIN_ARM_V - 0.1f; /* low, not critical */
	tick();

	zassert_equal(out.state, FC_ARMING_ARMED);
	zassert_true(out.motors_enabled);
	zassert_equal(out.block_reasons,
		      FC_ARMING_BLOCK_THROTTLE_HIGH | FC_ARMING_BLOCK_GYRO_NOT_CAL |
			      FC_ARMING_BLOCK_BATTERY_LOW);
	zassert_equal(out.events, 0U);
}

ZTEST(fc_arming, test_stays_armed_while_switch_held_on)
{
	arm();
	for (int i = 0; i < 1000; i++) {
		tick();
		zassert_equal(out.state, FC_ARMING_ARMED);
	}
}

/* ------------------------------------------------------------------------- */
/* Disarming                                                                  */
/* ------------------------------------------------------------------------- */

ZTEST(fc_arming, test_switch_off_disarms_at_full_throttle)
{
	arm();
	in.throttle = 1.0f;
	tick();

	in.arm_switch = false;
	tick();
	zassert_equal(out.state, FC_ARMING_DISARMED);
	zassert_false(out.motors_enabled);
	zassert_equal(out.events, FC_ARMING_EVENT_DISARMED);
}

ZTEST(fc_arming, test_disarmed_event_is_one_shot)
{
	arm();
	in.arm_switch = false;
	tick();
	tick();
	zassert_equal(out.state, FC_ARMING_DISARMED);
	zassert_equal(out.events, 0U);
}

ZTEST(fc_arming, test_can_rearm_after_disarm)
{
	arm();
	in.arm_switch = false;
	tick();
	in.arm_switch = true;
	tick();
	zassert_equal(out.state, FC_ARMING_ARMED);
	zassert_equal(out.events, FC_ARMING_EVENT_ARMED);
}

/* ------------------------------------------------------------------------- */
/* Entering failsafe                                                          */
/* ------------------------------------------------------------------------- */

ZTEST(fc_arming, test_rc_loss_while_armed_enters_failsafe)
{
	arm();
	step_with_rc_age(TEST_RC_TIMEOUT_US);

	zassert_equal(out.state, FC_ARMING_FAILSAFE);
	zassert_false(out.motors_enabled);
	zassert_equal(out.events, FC_ARMING_EVENT_FAILSAFE);
	zassert_equal(out.failsafe_reasons, FC_ARMING_FAILSAFE_RC_LOST);
}

ZTEST(fc_arming, test_rc_gap_below_timeout_stays_armed)
{
	arm();
	step_with_rc_age(TEST_RC_TIMEOUT_US - 1U);
	zassert_equal(out.state, FC_ARMING_ARMED);
}

/* Realistic loss: frames stop arriving, the loop keeps ticking. */
ZTEST(fc_arming, test_rc_frames_stop_enters_failsafe_after_timeout)
{
	uint64_t ticks_to_timeout = TEST_RC_TIMEOUT_US / TICK_US;

	arm();
	for (uint64_t i = 1; i < ticks_to_timeout; i++) {
		tick_no_rc();
		zassert_equal(out.state, FC_ARMING_ARMED, "failsafe too early");
	}
	tick_no_rc();
	zassert_equal(out.state, FC_ARMING_FAILSAFE);
}

ZTEST(fc_arming, test_imu_fault_while_armed_enters_failsafe)
{
	arm();
	in.imu_healthy = false;
	tick();
	zassert_equal(out.state, FC_ARMING_FAILSAFE);
	zassert_equal(out.failsafe_reasons, FC_ARMING_FAILSAFE_IMU_FAULT);
	zassert_equal(out.events, FC_ARMING_EVENT_FAILSAFE);
}

ZTEST(fc_arming, test_battery_critical_while_armed_enters_failsafe)
{
	arm();
	in.battery_voltage_v = TEST_BATT_CRITICAL_V - 0.1f;
	tick();
	zassert_equal(out.state, FC_ARMING_FAILSAFE);
	zassert_equal(out.failsafe_reasons, FC_ARMING_FAILSAFE_BATTERY_CRITICAL);
}

ZTEST(fc_arming, test_battery_at_critical_threshold_stays_armed)
{
	arm();
	in.battery_voltage_v = TEST_BATT_CRITICAL_V;
	tick();
	zassert_equal(out.state, FC_ARMING_ARMED);
}

ZTEST(fc_arming, test_multiple_failsafe_causes_all_reported)
{
	arm();
	in.imu_healthy = false;
	in.battery_voltage_v = 0.0f;
	step_with_rc_age(TEST_RC_TIMEOUT_US);

	zassert_equal(out.state, FC_ARMING_FAILSAFE);
	zassert_equal(out.failsafe_reasons, ALL_FAILSAFE_BITS);
}

/* Disarmed: a lost link only blocks arming, no failsafe (rule table). */
ZTEST(fc_arming, test_rc_loss_while_disarmed_only_blocks)
{
	clear_boot_latch();
	step_with_rc_age(TEST_RC_TIMEOUT_US);

	zassert_equal(out.state, FC_ARMING_DISARMED);
	zassert_equal(out.failsafe_reasons, 0U);
	zassert_equal(out.events, 0U);
	zassert_equal(out.block_reasons, FC_ARMING_BLOCK_NO_RC_LINK);
}

ZTEST(fc_arming, test_imu_fault_while_disarmed_only_blocks)
{
	clear_boot_latch();
	in.imu_healthy = false;
	tick();
	zassert_equal(out.state, FC_ARMING_DISARMED);
	zassert_equal(out.events, 0U);
}

/* Decision D5. */
ZTEST(fc_arming, test_switch_off_and_rc_loss_same_tick_disarm_wins)
{
	arm();
	in.arm_switch = false;
	step_with_rc_age(TEST_RC_TIMEOUT_US);

	zassert_equal(out.state, FC_ARMING_DISARMED);
	zassert_equal(out.events, FC_ARMING_EVENT_DISARMED);
	zassert_equal(out.failsafe_reasons, 0U);
}

/* ------------------------------------------------------------------------- */
/* In failsafe                                                                */
/* ------------------------------------------------------------------------- */

ZTEST(fc_arming, test_failsafe_event_is_one_shot)
{
	enter_failsafe_rc_lost();
	tick_no_rc();
	zassert_equal(out.state, FC_ARMING_FAILSAFE);
	zassert_equal(out.events, 0U);
}

ZTEST(fc_arming, test_failsafe_motors_stay_off)
{
	enter_failsafe_rc_lost();
	for (int i = 0; i < 1000; i++) {
		tick_no_rc();
		zassert_false(out.motors_enabled);
	}
}

/* Link back but switch still on: stay in failsafe, reason still latched. */
ZTEST(fc_arming, test_link_restored_with_switch_on_stays_failsafe)
{
	enter_failsafe_rc_lost();
	for (int i = 0; i < 100; i++) {
		tick();
		zassert_equal(out.state, FC_ARMING_FAILSAFE);
		zassert_equal(out.failsafe_reasons, FC_ARMING_FAILSAFE_RC_LOST);
	}
}

ZTEST(fc_arming, test_switch_off_without_link_stays_failsafe)
{
	enter_failsafe_rc_lost();
	in.arm_switch = false;
	tick_no_rc();
	zassert_equal(out.state, FC_ARMING_FAILSAFE);
}

/* Decision D6. */
ZTEST(fc_arming, test_leaves_failsafe_with_link_and_switch_off)
{
	enter_failsafe_rc_lost();
	in.arm_switch = false;
	tick();

	zassert_equal(out.state, FC_ARMING_DISARMED);
	zassert_equal(out.events, FC_ARMING_EVENT_DISARMED);
	zassert_equal(out.failsafe_reasons, 0U);
}

ZTEST(fc_arming, test_failsafe_reason_latched_after_cause_clears)
{
	arm();
	in.imu_healthy = false;
	tick();
	zassert_equal(out.state, FC_ARMING_FAILSAFE);

	in.imu_healthy = true;
	tick();
	zassert_equal(out.state, FC_ARMING_FAILSAFE);
	zassert_equal(out.failsafe_reasons, FC_ARMING_FAILSAFE_IMU_FAULT);
}

/* Non-RC failsafe with the link healthy: switch off is enough to leave. */
ZTEST(fc_arming, test_leaves_imu_failsafe_with_switch_off)
{
	arm();
	in.imu_healthy = false;
	tick();

	in.arm_switch = false;
	tick();
	zassert_equal(out.state, FC_ARMING_DISARMED);
	zassert_equal(out.block_reasons, FC_ARMING_BLOCK_IMU_UNHEALTHY);
}

ZTEST(fc_arming, test_rearm_after_failsafe_needs_fresh_edge)
{
	enter_failsafe_rc_lost();
	in.arm_switch = false;
	tick();
	zassert_equal(out.state, FC_ARMING_DISARMED);

	in.arm_switch = true;
	tick();
	zassert_equal(out.state, FC_ARMING_ARMED);
	zassert_equal(out.events, FC_ARMING_EVENT_ARMED);
}

/* ------------------------------------------------------------------------- */
/* Output hygiene                                                             */
/* ------------------------------------------------------------------------- */

/* Every field of out must be written on every step, so stale or garbage
 * contents never leak through.
 */
ZTEST(fc_arming, test_step_overwrites_all_outputs)
{
	memset(&out, 0xA5, sizeof(out));
	step();

	zassert_equal(out.state, FC_ARMING_DISARMED);
	zassert_false(out.motors_enabled);
	zassert_equal(out.events, 0U);
	zassert_equal(out.failsafe_reasons, 0U);
	zassert_equal(out.block_reasons, 0U);
}

ZTEST(fc_arming, test_same_inputs_give_same_outputs)
{
	fc_arming_ctx_t ctx_b;
	fc_arming_output_t out_b;

	fc_arming_init(&ctx_b);
	clear_boot_latch();
	/* Replay the same tick on the second context. */
	fc_arming_step(&cfg, &ctx_b, &in, &out_b);

	zassert_equal(out.state, out_b.state);
	zassert_equal(out.motors_enabled, out_b.motors_enabled);
	zassert_equal(out.block_reasons, out_b.block_reasons);
	zassert_equal(out.failsafe_reasons, out_b.failsafe_reasons);
	zassert_equal(out.events, out_b.events);
}