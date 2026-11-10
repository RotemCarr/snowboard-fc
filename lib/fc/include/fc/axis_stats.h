#ifndef FC_AXIS_STATS_H
#define FC_AXIS_STATS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fc_axis_stats {
	int32_t  offset;   /* first sample; everything is accumulated relative to it */
	int64_t  sum;      /* Σ (x - offset) */
	uint64_t sum_sq;   /* Σ (x - offset)² */
	uint32_t count;    /* samples added */
	bool     overflow; /* sticky: set if any accumulator would have overflowed */
} fc_axis_stats_t;

typedef struct {
uint32_t count;
int32_t  mean;  /* rounded to nearest */
uint32_t rms;   /* population std dev about the mean, rounded to nearest */
} fc_axis_stats_result_t;

void fc_axis_stats_init(fc_axis_stats_t *s);
void fc_axis_stats_add(fc_axis_stats_t *s, int32_t sample);
/* 0 on success, -EINVAL if no samples, -EOVERFLOW if the result can't be trusted */
int  fc_axis_stats_result(const fc_axis_stats_t *s, fc_axis_stats_result_t *out);

#ifdef __cplusplus
}
#endif

#endif /* FC_AXIS_STATS_H */
