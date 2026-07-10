// TVMDecoder_Extern.cpp

#include "TVMDecoder.h"
#include "PlaybackManager.h"
#include "TVMLogger.h"
#include <cstring>
#include <vector>
#include <algorithm>
#include <mutex>

std::shared_ptr<PlaybackManager> g_playbackManager;

extern "C" {

/**
 * @brief InitializePlaybackManager: Create a new playback manager
 * @param path: A file path for the
 * @param memLoad
 * @param decodeLoad
 * @param enableLogging
 * @return
 */
bool InitializePlaybackManager(const char* path, int memLoad, int decodeLoad, bool enableLogging) {
    try {
        g_playbackManager = std::make_shared<PlaybackManager>(
            std::string(path), memLoad, decodeLoad,  enableLogging);
        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

/**
 * @brief AdvanceSubSequence: Advance to the next SubSequence
 */
bool AdvanceSubSequence() {
    if (g_playbackManager) {
        return g_playbackManager->AdvanceSubSequence();
    }
    else {
        return false;
    }
}

/**
 * @brief LoadSubsequence: Load a SubSequence into memory
 */
void LoadSubSequence(int subSequence) {
    if (!g_playbackManager) return;;
    g_playbackManager->LoadSubSequence(subSequence);
}

/**
 * @brief DecodeSubsequence: Decode a SubSequence in memory
 */
void DecodeSubSequence(int subSequence) {
    if (!g_playbackManager) return;;
    g_playbackManager->DecodeSubSequence(subSequence);
}

int getSubSequenceCount(){
    return  g_playbackManager->getSubSequenceCount();
}

/**
 * @brief FetchFrame: Return the vertice positions for a given frame.
 */
void FetchFrame(int frameIndex, float* outVertices) {
    if (!g_playbackManager || !outVertices) return;
    try {
        const auto& deformed = g_playbackManager->fetchFrame(frameIndex);
        if (deformed.empty()) return; // Handle race condition
        for (size_t i = 0; i < deformed.size(); ++i) {
            outVertices[i * 3 + 0] = static_cast<float>(deformed[i].x());
            outVertices[i * 3 + 1] = static_cast<float>(deformed[i].y());
            outVertices[i * 3 + 2] = static_cast<float>(deformed[i].z());
        }
    } catch (...) {
        // Handle error
    }
}

/**
 * @brief GetCurrentDecoderTotalFrames: Returns the total frames for the current SubSequence
 */
int GetCurrentDecoderTotalFrames() {
    if (!g_playbackManager) return 0;
    auto decoder = g_playbackManager->getCurrentDecoder();
    if (!decoder) return 0;
    return decoder->GetTotalFrames();
}

/**
 * @brief GetCurrentDecoderVertexCount: Get the current SubSeqeunce meshes vertex count
 */
int GetCurrentDecoderVertexCount() {
    if (!g_playbackManager) return 0;
    auto decoder = g_playbackManager->getCurrentDecoder();
    if (!decoder) return 0;
    return decoder->GetVertexCount();
}


/**
 * @brief IsPlaybackManagerLoaded: Checks if the playback manager was initialized.
 */
bool IsPlaybackManagerLoaded() {
    return g_playbackManager != nullptr;
}

} // extern "C"
