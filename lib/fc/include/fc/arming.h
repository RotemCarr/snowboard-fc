/*
 * fc/arming.h - arming state machine.
 *
 * Pure logic: no Zephyr includes, no clock, no globals. The caller supplies
 * the time and all inputs on every call, so the module is deterministic and
 * unit-testable on native_sim.
 *
 * Transition rules (evaluated once per fc_arming_step call):
 *
 *   DISARMED -> ARMED     arm switch rising edge, with no block reasons set
 *   ARMED    -> DISARMED  arm switch off (always immediate, any throttle)
 *   ARMED    -> FAILSAFE  RC link timeout, IMU unhealthy or battery critical
 *   FAILSAFE -> DISARMED  RC link valid again AND arm switch off
 *
 * Conditions such as a lost link only block arming while DISARMED. They
 * cause FAILSAFE only from ARMED. Motors are enabled only in ARMED.
 * Failsafe policy is "motors off"; changing that needs a new RFC.
 * The module starts with the arm switch assumed on (see fc_arming_init), so
 * a switch left on at power-up never arms. It must go off, then on.
 */

#ifndef FC_ARMING_H
#define FC_ARMING_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum fc_arming_state {
    FC_ARMING_DISARMED,
    FC_ARMING_ARMED,
    FC_ARMING_FAILSAFE,
} fc_arming_state_t;

/* Reasons arming is currently refused. Recomputed every step. */
#define FC_ARMING_BLOCK_THROTTLE_HIGH      (1U << 0)
#define FC_ARMING_BLOCK_NO_RC_LINK         (1U << 1)
#define FC_ARMING_BLOCK_IMU_UNHEALTHY      (1U << 2)
#define FC_ARMING_BLOCK_GYRO_NOT_CAL       (1U << 3)
#define FC_ARMING_BLOCK_BATTERY_LOW        (1U << 4)
#define FC_ARMING_BLOCK_SWITCH_NOT_CYCLED  (1U << 5)

/* Reasons for entering FAILSAFE. Latched until the next disarm. */
#define FC_ARMING_FAILSAFE_RC_LOST          (1U << 0)
#define FC_ARMING_FAILSAFE_IMU_FAULT        (1U << 1)
#define FC_ARMING_FAILSAFE_BATTERY_CRITICAL (1U << 2)

/* One-shot flags, valid only for the step that produced them. */
#define FC_ARMING_EVENT_ARMED      (1U << 0)
#define FC_ARMING_EVENT_DISARMED   (1U << 1)
#define FC_ARMING_EVENT_FAILSAFE   (1U << 2)

/* Inputs: one snapshot per call. */
typedef struct fc_arming_input {
    uint64_t now_us;                /* monotonic time, microseconds */
    uint64_t rc_last_frame_us;      /* time of last valid RC frame, same clock */
    bool arm_switch;                /* decoded by the caller from the RC channel */
    float throttle;                 /* 0.0 .. 1.0; NaN or out of range counts as high */
    bool imu_healthy;               /* e.g. no FIFO overrun, samples on schedule */
    bool gyro_calibrated;
    float battery_voltage_v;        /* volts */
} fc_arming_input_t;

/* Thresholds. Fill with fc_arming_config_default() and override as needed. */
typedef struct fc_arming_config {
    float throttle_low_max;         /* highest throttle that counts as "low" */
    uint32_t rc_timeout_us;
    float battery_min_arm_v;
    float battery_critical_v;
} fc_arming_config_t;

/* Internal state. Callers allocate it (static is fine); fields are private. */
typedef struct fc_arming_ctx {
    fc_arming_state_t state;
    bool prev_arm_switch;
    bool needs_switch_off;
    uint32_t failsafe_reasons;
} fc_arming_ctx_t;

/* Outputs of one step. */
typedef struct fc_arming_output {
    bool motors_enabled;
    fc_arming_state_t state;
    uint32_t block_reasons;         /* FC_ARMING_BLOCK_* */
    uint32_t failsafe_reasons;      /* FC_ARMING_FAILSAFE_*, latched */
    uint32_t events;                /* FC_ARMING_EVENT_*, one-shot */
} fc_arming_output_t;

/* Fills cfg with default thresholds. */
void fc_arming_config_default(fc_arming_config_t *cfg);

/* Puts ctx in DISARMED with the arm switch assumed on (must cycle before arming). */
void fc_arming_init(fc_arming_ctx_t *ctx);

/* Advances the state machine by one tick and fills out. */
void fc_arming_step(const fc_arming_config_t *cfg, fc_arming_ctx_t *ctx,
                    const fc_arming_input_t *in, fc_arming_output_t *out);

#ifdef __cplusplus
}
#endif

#endif /* FC_ARMING_H */