#pragma once

#include "data_type.hpp"
#include "jsoncpp/json/json.h"
#include "open3d/Open3D.h"
#include "pipe.hpp"

CalibData calibration_loader(
    Json::Value jsonConf,
    uint32_t idx,
    std::string serial_num,
    PipeDataInCollectionOnce<CalibData>* const calib_data_out);
