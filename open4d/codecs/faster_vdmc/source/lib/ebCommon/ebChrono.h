/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2023, ISO/IEC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of the ISO/IEC nor the names of its contributors may
 *    be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _EB_CHRONO_H_
#define _EB_CHRONO_H_

// internal headers

// Windows systems
#if defined(WIN32)
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
// Unix systems
#else
#include <time.h>
#endif

namespace eb {

    /* C++ version not precise enough, depending on teh compiler
    inline std::chrono::steady_clock::time_point now(void) {
        return std::chrono::steady_clock::now();
    }
    // return time elapsed since t in milliseconds
    inline double elapsed(std::chrono::steady_clock::time_point t1) {
        return (double)(std::chrono::duration_cast<std::chrono::milliseconds>(now() - t1).count());
    }
    */

#if defined(WIN32)
    extern double _timeFreq;
#endif

    inline double now(void) {

        double res;
#if defined(WIN32)
        LARGE_INTEGER perfCount;
        QueryPerformanceCounter(&perfCount);
        res = (double)((1.0 / _timeFreq) * perfCount.QuadPart);
#else
        struct timespec abstime;
        clock_gettime(CLOCK_REALTIME, &abstime);
        res = (double)abstime.tv_sec * 1000.0 + (double)abstime.tv_nsec / 1000000.0;
#endif

        return res;
    }
    // return time elapsed since t in milliseconds
    inline double elapsed(double t1) {
        return now() - t1;
    }

};  // namespace mm

#endif

