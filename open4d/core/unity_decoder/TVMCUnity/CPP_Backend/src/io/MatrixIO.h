#pragma once

#include <string>
#include <Eigen/Dense>

namespace MatrixIO {
    /**
     * @brief loadtxt: Load a .txt file as a matrix for use with the decoder.
     * @param filename: A string corresponding to the filepath of the file we are reading.
     * @return A convererted matrix based off the text.
     */
    Eigen::MatrixXd loadtxt(const std::string& filename);

    /**
     * @brief LoadDeltaTrajectories: The the dekta trajectories .bin file into a matrix for
     *        use with the decoder.
     * @param bin_file_path: A string corresponding to the file path for the file we will load.
     * @return A convererted matrix based off the file.
     */
    Eigen::MatrixXd LoadDeltaTrajectories(const std::string& bin_file_path);
}
