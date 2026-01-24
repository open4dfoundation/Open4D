// PlaybackManager.h

#pragma once
#include <memory>
#include <string>
#include <mutex> // Make sure this is included
#include "TVMDecoder.h"

class PlaybackManager {
public:
    /**
     * @brief PlaybackManager: Constructor
     * @param path: Takes a string that is the path to the folder of the encoded sequence we are decoding
     * @param memLoad: The amount of subsequences that we will pre-load into memory from IO
     * @param decodeLoad: The amount of subseqences that we will pre-decode before playback (memLoad must be greater then decodeLoad)
     * @param enableLogging: A bool to enable logging messages
     */
    PlaybackManager(const std::string& path, int memLoad, int decodeLoad, bool enableLogging);

    /**
     * @brief AdvanceSubSequence: Advance to the next subsequence in the encoded sequence
     * @return success or failure
     */
    bool AdvanceSubSequence();

    /**
     * @brief LoadSubSequence: Load the files for a given subsequence into stored memory
     * @param subSequence: The subseqeunce we are loading (0-indexed)
     */
    void LoadSubSequence(int subSequence);

    /**
     * @brief DecodeSubSequence Decode a given subsequence from memory
     * @param subSequence: The subseqeunce we are decoding (0-indexed)
     */
    void DecodeSubSequence(int subSequence);
    /**
     * @brief fetchFrame: Get the vertex values for a given frame from the active decoder.
     * @param frame: The frame we are fetching (0-indexed)
     * @return A vector holder the frame values.
     */
    std::vector<Eigen::Vector3d, Eigen::aligned_allocator<Eigen::Vector3d>>fetchFrame(int frame);

    //getter functions
    int getSubSequenceCount();
    std::shared_ptr<TVMDecoder::Decoder> getCurrentDecoder();

    //cleanup
    ~PlaybackManager();
private:
    std::string sequenceDirectory;
    std::vector<std::shared_ptr<TVMDecoder::Decoder>> activeDecoders;
    int currentSubSequence;
    int subSequenceCount;
    int preLoad;
    int subSequenceLength;
    std::mutex activeListMutex; // The member mutex
};
