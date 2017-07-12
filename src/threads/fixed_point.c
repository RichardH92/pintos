#include "threads/fixed_point.h"

static int f = 1 << FRACTION_BITS;

/* Convert fixed_point_t to int. */
int fixed_to_int(fixed_point_t x, int round_nearest) {
	int n;
	if (round_nearest) {
		if (x.v >= 0) {
			n = (x.v + (f / 2)) / f;
		} else {
			n = (x.v - (f / 2)) / f;
		}
	} else {
		n = x.v / f;
	}
	return n;
}

/* Convert int to fixed_point_t. */
fixed_point_t int_to_fixed(int n) {
	fixed_point_t x;
	x.v = n * f;
	return x;
}

/* Add two fixed point values. */
fixed_point_t fixed_add(fixed_point_t x, fixed_point_t y) {
	fixed_point_t z;
	z.v = x.v + y.v;
	return z;
}

/* Subtract two fixed point values. */
fixed_point_t fixed_sub(fixed_point_t x, fixed_point_t y) {
	fixed_point_t z;
	z.v = x.v - y.v;
	return z;
}

/* Multiply two fixed point values. */
fixed_point_t fixed_mult(fixed_point_t x, fixed_point_t y) {
	fixed_point_t z;
	z.v = ((int64_t) x.v) * y.v / f;
	return z;
}

/* Divide two fixed point values. */
fixed_point_t fixed_div(fixed_point_t x, fixed_point_t y) {
	fixed_point_t z;
	z.v = ((int64_t) x.v) * f / y.v;
	return z;
}

/* Add a fixed point value and an int value. */
fixed_point_t fixed_int_add(fixed_point_t x, int n) {
	return fixed_add(x, int_to_fixed(n));
}

/* Subtract an int value from a fixed point value. */
fixed_point_t fixed_int_sub(fixed_point_t x, int n) {
	return fixed_sub(x, int_to_fixed(n));
}

/* Multiply a fixed point value with an int value. */
fixed_point_t fixed_int_mult(fixed_point_t x, int n) {
	return fixed_mult(x, int_to_fixed(n));
}

/* Divide a fixed point value by an int value. */
fixed_point_t fixed_int_div(fixed_point_t x, int n) {
	return fixed_div(x, int_to_fixed(n));
}
