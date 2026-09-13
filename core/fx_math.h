/* fx_math.h — math shared by the CPU and GPU (Metal) paths.
 *
 * The Metal implementation (core/metal_fx.m) must be byte-identical to the
 * CPU core (§5 determinism), and libm transcendental results are not
 * guaranteed equal across runtimes — so every transcendental the particle
 * effects use goes through here, and the MSL source mirrors these exact
 * constants and operation orders (do not "simplify" one side only).
 */

#ifndef FX_MATH_H
#define FX_MATH_H

/* float(3.141592653589793…) — same constant in the MSL mirror. */
#define FX_PI 3.14159265f

/* sin: range reduction to [-π, π] + degree-8 Taylor polynomial.
 * Only exact ops + one correctly-rounded reduction (rint), same float
 * constants on both sides ⇒ bit-identical CPU/GPU. |err| < 3e-5 — far
 * below one pixel at the sub-pixel scales these effects use. */
static inline float fx_sinf(float x)
{
    float k = rintf(x * 0.1591549431f); /* round(x / 2π), to even */
    float r = x - k * 6.2831853072f;    /* 2π as float */
    float r2 = r * r;
    return r * (1.0f + r2 * (-0.1666666716f +
                            r2 * (0.0083333339f +
                            r2 * (-0.0001984127f +
                            r2 * 0.0000027557f))));
}

#endif /* FX_MATH_H */
