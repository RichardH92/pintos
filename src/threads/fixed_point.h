#ifndef FIXED_POINT_H
#define FIXED_POINT_H

#include <inttypes.h>

typedef struct {
	int v;
} fixed_point_t;

#define FRACTION_BITS 14

/* Converting between int and fixed_point_t. */
int fixed_to_int (fixed_point_t x, int round_nearest);
fixed_point_t int_to_fixed (int n);

/* Basic arithmetic between fixed point numbers. */
fixed_point_t fixed_add (fixed_point_t x, fixed_point_t y);
fixed_point_t fixed_sub (fixed_point_t x, fixed_point_t y);
fixed_point_t fixed_mult (fixed_point_t x, fixed_point_t y);
fixed_point_t fixed_div (fixed_point_t x, fixed_point_t y);

/* Basic arithmetic between fixed point and int numbers. */
fixed_point_t fixed_int_add (fixed_point_t x, int n);
fixed_point_t fixed_int_sub (fixed_point_t x, int n);
fixed_point_t fixed_int_mult (fixed_point_t x, int n);
fixed_point_t fixed_int_div (fixed_point_t x, int n);

#endif