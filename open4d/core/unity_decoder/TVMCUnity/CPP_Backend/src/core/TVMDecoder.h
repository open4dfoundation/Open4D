#pragma once
#include <string>
#include <vector>
#include <Eigen/Sparse>
#include "SimpleMesh.h"


namespace TVMDecoder {

class Decoder {
public:
    // Required for proper memory alignment when using Eigen in containers
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW

    /**
     * @brief Decoder: constructor
     * @param name: A string corresponding to the name of the decoder to store in the manager.
     * @param out: The out file path for writing files.
     */
    Decoder(const std::string& name);
    ~Decoder();

    /**
     * @brief LoadSequence: Loads an encoded mesh sequence into memory.
     * @param directoryPath: The file path corelating to the sequence
     */
    void LoadSequence(const std::string& directoryPath);

    /**
     * @brief DecodeSequence: Decode the loaded sequence.
     * @return A bool correlating to success.
     */
    bool DecodeSequence();

    /**
     * @brief DecodeObjs: Output a sequence of .OBJs from the encoded sequence
     * @return A vector of strings that represent the files paths for the decoded .OBJ files.
     *         NOTE: The vector will return null if the sequence has not been loaded.
     */
    std::vector<std::string> DecodeObjs();

    /**
     * @brief ApplyDisplacementsToFrame: Fetches the stored decoded displaments for a specific frame.
     * @param frameIndex: An integer representing the index for the frame we are fetching (0-based)
     * @throws RunTimeError: If the sequence hasn't been loaded or the displacement vertex count doesnt
     *         match the reference frame vertex count.
     * @throws OutOfRange: If the provided frame is outside of the range of frames in the sequence.
     * @return A vector storing the final displaced mesh data.
     *         NOTE: The vector will return null if the sequence has not been loaded.
     */
    std::vector<Eigen::Vector3d, Eigen::aligned_allocator<Eigen::Vector3d>>
    ApplyDisplacementToFrame(int frameIndex) const;

    // Getter functions
    const std::string& GetName() const { return decoderName; }
    int GetTotalFrames() const { return totalFrames; }
    bool IsDecoded() const { return isDecoded; }
    int GetVertexCount() const { return verticesPerFrame; }
    std::vector<int> GetTriangleIndicesFlat() const { return triangleIndicesFlat; }
    const double* GetReferenceVertices() { return referenceVertexBuffer.data(); }

    // Memory cleanup
    void Clear();

private:
    // Helper function for common processing after data is loaded
    bool ProcessLoadedData();

    std::string decoderName;

    // Decoding data
    SimpleMesh::Mesh decodedReferenceMesh;
    Eigen::MatrixXd dHat, bMatrix, tMatrix, S_hat, T_hat, tMean;
    Eigen::SparseMatrix<double> l_star;

    // Decoded buffers
    std::vector<std::vector<Eigen::Vector3d, Eigen::aligned_allocator<Eigen::Vector3d>>> decodedFrames;
    std::vector<double> decodedVertexBuffer;
    std::vector<double> referenceVertexBuffer;
    std::vector<int> triangleIndicesFlat;
    std::vector<int> anchor_indices;

    // State
    int totalFrames = 0;
    int verticesPerFrame = 0;
    bool isDecoded = false;
};

} // namespace TVMDecoder
