#include <fc/axis_stats.h>
#include <errno.h>
#include <math.h>

void fc_axis_stats_init(fc_axis_stats_t *s)
{
	*s = (fc_axis_stats_t){0};
}

void fc_axis_stats_add(fc_axis_stats_t *s, int32_t sample)
{
	if (s->overflow) {
		return;
	}

	if (s->count == 0) {
		s->offset = sample;
	}

	int64_t d = (int64_t)sample - s->offset;
	uint64_t m = (uint64_t)(d < 0 ? -d : d);
	uint64_t d2 = m * m;

	if (__builtin_add_overflow(s->sum, d, &s->sum)) {
		s->overflow = true;
	}

	if (__builtin_add_overflow(s->sum_sq, d2, &s->sum_sq)) {
		s->overflow = true;
	}

	if (__builtin_add_overflow(s->count, 1, &s->count)) {
		s->overflow = true;
	}
}

int fc_axis_stats_result(const fc_axis_stats_t *s, fc_axis_stats_result_t *out)
{
	if (s->overflow) {
		return -EOVERFLOW;
	}

	if (s->count == 0) {
		return -EINVAL;
	}

	const double n = (double)s->count;
	const double mean_d = (double)s->sum / n;     /* mean of the differences */
	double var = (double)s->sum_sq / n - mean_d * mean_d;

	if (var < 0.0) {
		var = 0.0; /* rounding can push a ~0 variance slightly negative */
	}

	out->count = s->count;
	out->mean = (int32_t)lround((double)s->offset + mean_d);
	out->rms = (uint32_t)lround(sqrt(var));

	return 0;
}
