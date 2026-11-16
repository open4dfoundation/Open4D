// System.Random(seed) from .NET (Knuth's subtractive generator), so a seeded
// run places the first frame's centers exactly like the C# tracker.
#pragma once

#include <climits>
#include <cstdlib>

namespace arapcuda {

class DotNetRandom
{
public:
    explicit DotNetRandom(int seed)
    {
        const int MSEED = 161803398;
        int subtraction = seed == INT_MIN ? INT_MAX : std::abs(seed);
        int mj = MSEED - subtraction;
        seeds_[55] = mj;
        int mk = 1;
        for (int i = 1; i < 55; i++)
        {
            int ii = (21 * i) % 55;
            seeds_[ii] = mk;
            mk = mj - mk;
            if (mk < 0)
                mk += MBIG;
            mj = seeds_[ii];
        }
        for (int k = 1; k < 5; k++)
            for (int i = 1; i < 56; i++)
            {
                seeds_[i] -= seeds_[1 + (i + 30) % 55];
                if (seeds_[i] < 0)
                    seeds_[i] += MBIG;
            }
    }

    double next_double() { return sample() * (1.0 / MBIG); }
    int next(int max_value) { return (int)(next_double() * max_value); }

private:
    static constexpr int MBIG = INT_MAX;
    int seeds_[56] = {};
    int inext_ = 0, inextp_ = 21;

    int sample()
    {
        if (++inext_ >= 56)
            inext_ = 1;
        if (++inextp_ >= 56)
            inextp_ = 1;
        int r = seeds_[inext_] - seeds_[inextp_];
        if (r == MBIG)
            r--;
        if (r < 0)
            r += MBIG;
        seeds_[inext_] = r;
        return r;
    }
};

} // namespace arapcuda
