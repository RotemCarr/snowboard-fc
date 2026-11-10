#include <zephyr/ztest.h>
#include <errno.h>
#include "fc/axis_stats.h"

static void add_pattern(fc_axis_stats_t *s, const int32_t *pattern, size_t len,
			uint32_t repeats)
{
	for (uint32_t r = 0; r < repeats; r++) {
		for (size_t i = 0; i < len; i++) {
			fc_axis_stats_add(s, pattern[i]);
		}
	}
}

/* Fresh stats object, feed the pattern, return the result code. */
static int stats_of(const int32_t *pattern, size_t len, uint32_t repeats,
		    fc_axis_stats_result_t *out)
{
	fc_axis_stats_t s;

	fc_axis_stats_init(&s);
	add_pattern(&s, pattern, len, repeats);
	return fc_axis_stats_result(&s, out);
}

ZTEST(axis_stats, test_empty_returns_einval)
{
	fc_axis_stats_t s;
	fc_axis_stats_result_t out;

	fc_axis_stats_init(&s);
	zassert_equal(fc_axis_stats_result(&s, &out), -EINVAL);
}

ZTEST(axis_stats, test_single_sample)
{
	const int32_t pattern[] = {42};
	fc_axis_stats_result_t out;

	zassert_equal(stats_of(pattern, ARRAY_SIZE(pattern), 1, &out), 0);
	zassert_equal(out.count, 1);
	zassert_equal(out.mean, 42);
	zassert_equal(out.rms, 0);
}

ZTEST(axis_stats, test_constant_input_has_zero_rms)
{
	const int32_t pattern[] = {1000};
	const uint32_t repeats = 100;
	fc_axis_stats_result_t out;

	zassert_equal(stats_of(pattern, ARRAY_SIZE(pattern), repeats, &out), 0);
	zassert_equal(out.count, ARRAY_SIZE(pattern) * repeats);
	zassert_equal(out.mean, 1000);
	zassert_equal(out.rms, 0); /* constant signal has no noise */
}

ZTEST(axis_stats, test_symmetric_around_zero)
{
	const int32_t pattern[] = {-1, 1};
	const uint32_t repeats = 1000;
	fc_axis_stats_result_t out;

	zassert_equal(stats_of(pattern, ARRAY_SIZE(pattern), repeats, &out), 0);
	zassert_equal(out.count, ARRAY_SIZE(pattern) * repeats);
	zassert_equal(out.mean, 0); /* values symmetric around 0 average to 0 */
	zassert_equal(out.rms, 1);
}

ZTEST(axis_stats, test_large_positive_offset_keeps_precision)
{
	const int32_t pattern[] = {1000001, 999999};
	const uint32_t repeats = 1000;
	fc_axis_stats_result_t out;

	zassert_equal(stats_of(pattern, ARRAY_SIZE(pattern), repeats, &out), 0);
	zassert_equal(out.count, ARRAY_SIZE(pattern) * repeats);
	zassert_equal(out.mean, 1000000); /* values symmetric around 1,000,000 average to it */
	zassert_equal(out.rms, 1);
}

ZTEST(axis_stats, test_large_negative_offset_keeps_precision)
{
	const int32_t pattern[] = {-5000001, -4999999};
	const uint32_t repeats = 1000;
	fc_axis_stats_result_t out;

	zassert_equal(stats_of(pattern, ARRAY_SIZE(pattern), repeats, &out), 0);
	zassert_equal(out.count, ARRAY_SIZE(pattern) * repeats);
	zassert_equal(out.mean, -5000000); /* values symmetric around -5,000,000 average to it */
	zassert_equal(out.rms, 1);
}

ZTEST(axis_stats, test_textbook_mean_and_rms)
{
	const int32_t pattern[] = {2, 4, 4, 4, 5, 5, 7, 9};
	fc_axis_stats_result_t out;

	zassert_equal(stats_of(pattern, ARRAY_SIZE(pattern), 1, &out), 0);
	zassert_equal(out.count, ARRAY_SIZE(pattern));
	zassert_equal(out.mean, 5);
	zassert_equal(out.rms, 2);
}

ZTEST(axis_stats, test_full_run_at_gyro_noise_level)
{
	const int32_t pattern[] = {1745, -1745};
	const uint32_t repeats = 40000;
	fc_axis_stats_result_t out;

	zassert_equal(stats_of(pattern, ARRAY_SIZE(pattern), repeats, &out), 0);
	zassert_equal(out.count, ARRAY_SIZE(pattern) * repeats);
	zassert_equal(out.mean, 0);
	zassert_equal(out.rms, 1745);
}

ZTEST(axis_stats, test_full_scale_input_returns_eoverflow)
{
	const int32_t pattern[] = {34906585, -34906585};
	const uint32_t repeats = 40000;
	fc_axis_stats_result_t out;

	/* this pattern should overflow */
	zassert_equal(stats_of(pattern, ARRAY_SIZE(pattern), repeats, &out), -EOVERFLOW);
}

ZTEST(axis_stats, test_init_clears_overflow)
{
	const int32_t overflow[] = {34906585, -34906585};
	const int32_t textbook[] = {2, 4, 4, 4, 5, 5, 7, 9};
	fc_axis_stats_result_t out;
	fc_axis_stats_t s;

	/* drive the object into overflow */
	fc_axis_stats_init(&s);
	add_pattern(&s, overflow, ARRAY_SIZE(overflow), 40000);
	zassert_equal(fc_axis_stats_result(&s, &out), -EOVERFLOW);

	/* re-init: overflow cleared, no samples yet */
	fc_axis_stats_init(&s);
	zassert_equal(fc_axis_stats_result(&s, &out), -EINVAL);

	/* the same object now works normally */
	add_pattern(&s, textbook, ARRAY_SIZE(textbook), 1);
	zassert_equal(fc_axis_stats_result(&s, &out), 0);
	zassert_equal(out.count, ARRAY_SIZE(textbook));
	zassert_equal(out.mean, 5);
	zassert_equal(out.rms, 2);
}

ZTEST(axis_stats, test_error_leaves_output_untouched)
{
	const int32_t overflow[] = {34906585, -34906585};
	const fc_axis_stats_result_t sentinel = {.count = 123, .mean = 456, .rms = 789};
	fc_axis_stats_result_t out = sentinel;
	fc_axis_stats_t s;

	/* empty: -EINVAL */
	fc_axis_stats_init(&s);
	zassert_equal(fc_axis_stats_result(&s, &out), -EINVAL);
	zassert_mem_equal(&out, &sentinel, sizeof(out));

	/* overflow: -EOVERFLOW */
	zassert_equal(stats_of(overflow, ARRAY_SIZE(overflow), 40000, &out), -EOVERFLOW);
	zassert_mem_equal(&out, &sentinel, sizeof(out));
}

ZTEST(axis_stats, test_unrepresentative_first_sample)
{
	const int32_t pattern[] = {1000, 0, 0, 0};
	fc_axis_stats_result_t out;

	zassert_equal(stats_of(pattern, ARRAY_SIZE(pattern), 1, &out), 0);
	zassert_equal(out.count, ARRAY_SIZE(pattern));
	zassert_equal(out.mean, 250);
	zassert_equal(out.rms, 433);
}

ZTEST(axis_stats, test_positive_half_rounds_away_from_zero)
{
	const int32_t pattern[] = {0, 1};
	fc_axis_stats_result_t out;

	zassert_equal(stats_of(pattern, ARRAY_SIZE(pattern), 1, &out), 0);
	zassert_equal(out.count, ARRAY_SIZE(pattern));
	zassert_equal(out.mean, 1);
	zassert_equal(out.rms, 1);
}

ZTEST(axis_stats, test_negative_half_rounds_away_from_zero)
{
	const int32_t pattern[] = {-1, 0};
	fc_axis_stats_result_t out;

	zassert_equal(stats_of(pattern, ARRAY_SIZE(pattern), 1, &out), 0);
	zassert_equal(out.count, ARRAY_SIZE(pattern));
	zassert_equal(out.mean, -1);
	zassert_equal(out.rms, 1);
}

ZTEST(axis_stats, test_offset_preserves_precision_near_int32_limit)
{
	/* Without the offset, sum_sq = 8e18 + 2. At that size a double can only
	 * hold multiples of 1024, so the +2 (all of the noise) rounds away and
	 * the rms comes out 0. With the offset the differences are 0 and -2.
	 */
	const int32_t pattern[] = {2000000001, 1999999999};
	fc_axis_stats_result_t out;

	zassert_equal(stats_of(pattern, ARRAY_SIZE(pattern), 1, &out), 0);
	zassert_equal(out.count, ARRAY_SIZE(pattern));
	zassert_equal(out.mean, 2000000000);
	zassert_equal(out.rms, 1);
}

ZTEST_SUITE(axis_stats, NULL, NULL, NULL, NULL, NULL);
