#include "MatrixIO.h"
#include "TVMLogger.h"
#include <fstream>
#include <vector>
#include <stdexcept>
#include <iostream>

Eigen::MatrixXd MatrixIO::loadtxt(const std::string& filename) {
    // Load file
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filename);
    }

    std::vector<std::vector<double>> rows;
    std::string line;

    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        std::vector<double> row;
        size_t start = 0;

        while (start < line.size()) {
            // Skip whitespace
            while (start < line.size() && std::isspace(line[start])) ++start;
            if (start >= line.size()) break;

            // Find next delimiter (space or end)
            size_t end = start;
            while (end < line.size() && !std::isspace(line[end])) ++end;

            std::string token = line.substr(start, end - start);

            // Parse number
            if (!token.empty()) {
                try {
                    double val = std::stod(token);
                    row.push_back(val);
                } catch (const std::invalid_argument&) {
                    throw std::runtime_error("Invalid double in " + filename + ": " + token);
                } catch (const std::out_of_range&) {
                    throw std::runtime_error("Number out of range in " + filename + ": " + token);
                }
            }

            start = end;
        }

        // Add non-empty rows, ensuring consistent column count
        if (!row.empty()) {
            if (!rows.empty() && row.size() != rows[0].size()) {
                throw std::runtime_error("Inconsistent column count in: " + filename);
            }
            rows.push_back(row);
        }
    }

    if (rows.empty()) {
        throw std::runtime_error("No data found in: " + filename);
    }

    // Convert to Eigen matrix
    Eigen::MatrixXd mat(rows.size(), rows[0].size());
    for (size_t i = 0; i < rows.size(); ++i) {
        for (size_t j = 0; j < rows[i].size(); ++j) {
            mat(i, j) = rows[i][j];
        }
    }

    return mat;
}




Eigen::MatrixXd MatrixIO::LoadDeltaTrajectories(const std::string& bin_file_path) {
    //Load File
    std::ifstream file(bin_file_path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open binary displacement file.");
    }

    //Set Values
    int32_t numRows = 0, numCols = 0;
    file.read(reinterpret_cast<char*>(&numRows), sizeof(int32_t));
    file.read(reinterpret_cast<char*>(&numCols), sizeof(int32_t));
    LOG_INFO("[Decoder] Shape from header: ", numRows, " rows x ", numCols," cols");

    //Check for valid shape
    if (numRows <= 0 || numCols <= 0 || numCols > 1000 || numRows > 1000000) {
        LOG_ERROR("[Decoder] ❌ Invalid shape. Rejecting BIN.");
        throw std::runtime_error("Corrupt BIN: invalid dimensions");
    }

    const size_t totalValues = static_cast<size_t>(numRows) * numCols;
    const size_t byteCount = totalValues * sizeof(double);

    std::vector<double> buffer(totalValues);
    file.read(reinterpret_cast<char*>(buffer.data()), byteCount);

    //Project and return
    Eigen::Map<Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>> mat64(buffer.data(), numRows, numCols);
    Eigen::MatrixXd mat32 = mat64.cast<double>();

    return mat32;
}

