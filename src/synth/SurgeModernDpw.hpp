/*
 * Surge XT - a free and open source hybrid synthesizer,
 * built by Surge Synth Team
 *
 * Learn more at https://surge-synthesizer.github.io/
 *
 * Copyright 2018-2024, various authors, as described in the GitHub
 * transaction log.
 *
 * Surge XT is released under the GNU General Public Licence v3
 * or later (GPL-3.0-or-later). The license is found in the "LICENSE"
 * file in the root of this repository, or at
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 *
 * Surge was a commercial product from 2004-2018, copyright and ownership
 * held by Claes Johanson at Vember Audio during that period.
 * Claes made Surge open source in September 2018.
 *
 * All source for Surge XT is available at
 * https://github.com/surge-synthesizer/surge
 */

// Extracted no-sync DPW helpers, unchanged arithmetic. See README.md (Surge XT source provenance).
#pragma once
#include <cmath>
namespace nord::synth::surge {
enum MultiType {momt_triangle, momt_square, momt_sine};
constexpr double dpwOneOverSix = 1.0 / 6.0;

// Cubic saw generator (p^3 - p)/6, phase p01 in [0,1) mapped to [-1,1].
inline double dpwSawGen(double p01)
{
    double p = (p01 - 0.5) * 2.0;
    return (p * p * p - p) * dpwOneOverSix;
}

/*
 * DPW saw component value at phase p (in [0,1)) with per-sample phase increment
 * dp, using the backward 3-point stencil (p, p-dp, p-2dp) centered at p-dp.
 *
 * The cubic generator has zero fourth derivative, so away from the wrap its
 * numerical second difference is *exactly* the analytic saw at the (lagged)
 * stencil center - so we just return that and skip three cubic evaluations and
 * the divide. Only near the wrap (or when forced, e.g. under sync) do we run the
 * full numerical second difference. Result already has the 1/(4 dp^2) scale
 * folded in, matching (secondDifference * denom) in the caller.
 */
inline double dpwSawComp(double p, double dp, bool forceNumeric)
{
    if (!forceNumeric && p >= 3.0 * dp)
    {
        double pc = p - dp; // lagged stencil center, still in [0,1) here
        return (pc - 0.5) * 2.0;
    }

    double p1 = p - dp;
    p1 += (p1 < 0);
    double p2 = p - 2.0 * dp;
    p2 += (p2 < 0);
    return (dpwSawGen(p) + dpwSawGen(p2) - 2.0 * dpwSawGen(p1)) * (0.25 / (dp * dp));
}

/*
 * DPW multitype (triangle / square / sine) component value. Triangle and square
 * generators are degree <= 3, so the analytic second derivative at the lagged
 * center is exact away from their corners. The sine generator is a quartic, so
 * its numerical second difference is not analytically identical - sine always
 * takes the numerical path (left unchanged by this optimization).
 */
template <MultiType multitype>
inline double dpwMultiComp(double p, double dp, bool forceNumeric)
{
    bool numeric = forceNumeric || (multitype == momt_sine);

    if (!numeric)
    {
        // Fall back to numerical near a generator corner / wrap.
        if (multitype == momt_square)
            numeric = (p < 3.0 * dp) || (fabs(p - 0.5) < 3.0 * dp);
        if (multitype == momt_triangle)
            numeric = (fabs(p - 0.25) < 3.0 * dp) || (fabs(p - 0.75) < 3.0 * dp);
    }

    if (!numeric)
    {
        double pc = p - dp;
        pc += (pc < 0);
        double x = (pc - 0.5) * 2.0;

        if (multitype == momt_square)
        {
            return (x < 0) * 2.0 - 1.0;
        }
        if (multitype == momt_triangle)
        {
            double tp = x + 0.5;
            tp -= (tp > 1.0) * 2.0;
            double Q = 1.0 - (tp < 0) * 2.0;
            return 1.0 - 2.0 * Q * tp;
        }
    }

    // Numerical second difference of the generator (identical math to the
    // original per-sample path).
    double ph0 = p;
    double ph1 = p - dp;
    ph1 += (ph1 < 0);
    double ph2 = p - 2.0 * dp;
    ph2 += (ph2 < 0);

    double g[3];
    double phs[3] = {ph0, ph1, ph2};
    for (int s = 0; s < 3; ++s)
    {
        double pp = (phs[s] - 0.5) * 2.0;
        double p3 = pp * pp * pp;
        if (multitype == momt_square)
        {
            double Q = (pp < 0) * 2 - 1;
            g[s] = pp * (Q * pp + 1) * 0.5;
        }
        else if (multitype == momt_sine)
        {
            // double pos = 1.0 - std::signbit(pp);
            double modpos = 2.0 * (pp < 0) - 1.0;
            double p4 = p3 * pp;
            constexpr double oo3 = 1.0 / 3.0;

            /*
             * So...
             *
             * -(pos * (-p4 + 2 * p3 - p) + (pos - 1) * (-p4 - 2 * p3 + p)) * oo3
             *
             * Alright so p4 is:
             *
             * (pos * -p4 + (pos - 1) * -p4) == ( 1 - 2 * pos ) * p4
             *
             * p3 is:
             *
             * pos * 2 * p3 + (pos - 1) * -2 * p3
             * pos * 2 * p3 - pos * 2 + p3 + 2 * p3
             *       2 * p3
             *
             * p is:
             *
             * pos * -p + (pos - 1) + p
             * -pos * p + pos * p - p
             * or -p
             *
             * so our term is actually:
             *
             * -((1 - 2 * pos) * p4 + 2 * p3 - p) * oo3
             *
             * Moreover, pos is 1-signbit so (1 - 2 * pos) == (1 - 2 + 2 * signbit)
             * or 2 * signbit - 1
             */
            g[s] = -(modpos * p4 + 2 * p3 - pp) * oo3;
        }
        else // triangle
        {
            double tp = pp + 0.5;
            tp -= (tp > 1.0) * 2;
            double Q = 1 - (tp < 0) * 2;
            g[s] = (2.0 + tp * tp * (3.0 - 2.0 * Q * tp)) * dpwOneOverSix;
        }
    }
    return (g[0] + g[2] - 2.0 * g[1]) * (0.25 / (dp * dp));
}
}
