#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#include <debug.h>
#include <stdint.h>

typedef int fixed_point;

#define F_BITS 16384

/* Integer to fixed point number */
static inline fixed_point
itofp (const int x)
{
  return x * F_BITS;
}

/* Fixed point to integer */
static inline int
fptoi (const fixed_point x)
{
  return x / F_BITS;
}

/* Fixed point to integer (round) */
static inline int
fptoi_round (const fixed_point x)
{
  return x >= 0 ? (x + F_BITS / 2) / F_BITS :
                  (x - F_BITS / 2) / F_BITS;
}

static inline fixed_point
fp_add (const fixed_point a, const fixed_point b)
{
  return a + b;
}

static inline fixed_point
fp_sub (const fixed_point a, const fixed_point b)
{
  return a - b;
}

static inline fixed_point
fp_mul (const fixed_point a, const fixed_point b)
{
  return ((int64_t)a * b) / F_BITS;
}

static inline fixed_point
fp_div (const fixed_point a, const fixed_point b)
{
  return ((int64_t)a * F_BITS) / b;
}

static inline fixed_point
fp_int_add (const fixed_point a, const int b)
{
  return a + b * F_BITS;
}

static inline fixed_point
fp_int_sub (const fixed_point a, const int b)
{
  return a - b * F_BITS;
}

static inline fixed_point
fp_int_mul (const fixed_point a, const int b)
{
  return a * b;
}

static inline fixed_point
fp_int_div (const fixed_point a, const int b)
{
  return a / b;
}

#endif /* threads/fixed-point.h */