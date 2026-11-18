// Error reporting shared by the native translation units.
#pragma once

#include "arapcuda.h"

#include <cuda_runtime.h>

#include <string>

namespace arap {

extern thread_local std::string lastError;

inline int fail(int code, const std::string& msg)
{
    lastError = msg;
    return code;
}

} // namespace arap

#define CUDA_TRY(call)                                                                       \
    do {                                                                                     \
        cudaError_t err_ = (call);                                                           \
        if (err_ != cudaSuccess)                                                             \
            return arap::fail(ARAP_ERR_CUDA, std::string(#call ": ") + cudaGetErrorString(err_)); \
    } while (0)
