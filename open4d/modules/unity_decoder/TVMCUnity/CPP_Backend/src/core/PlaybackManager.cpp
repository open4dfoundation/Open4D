// DecoderManager.cpp

#include "PlaybackManager.h"
#include "TVMLogger.h"
#include "TVMDecoder.h"
#include <filesystem>
#include <mutex>

PlaybackManager::PlaybackManager(const std::string& path, int memLoad, int decodeLoad, bool enableLogging){
    TVMLogger::EnableLogging(enableLogging);
    if (path.empty()) {
        LOG_ERROR("[DecoderManager] ❌ Provided path is empty!");
    }
    sequenceDirectory = path;
    subSequenceCount = 0;
    currentSubSequence = 0;

    if (std::filesystem::exists(sequenceDirectory) && std::filesystem::is_directory(sequenceDirectory)) {
        for (const auto& entry : std::filesystem::directory_iterator(sequenceDirectory)) {
            if (entry.is_directory()) {
                subSequenceCount++;
            }
        }
    }
    activeDecoders.clear();
    LOG_INFO("✅Sequence directory set: " + path);
    for (int i = 1; i <= memLoad && i <= subSequenceCount; i++){
        LoadSubSequence(i);
        if (i <= decodeLoad) DecodeSubSequence(i);
    }
    currentSubSequence = 1;
    if (activeDecoders.size() <= 0){
        LOG_ERROR("[DecoderManager] ❌ Encoded sequence is empty!");
    }
    else {
        subSequenceLength = activeDecoders.front()->GetTotalFrames();
    }
    preLoad = memLoad;
}

PlaybackManager::~PlaybackManager(){
    activeDecoders.clear();
}

bool PlaybackManager::AdvanceSubSequence(){
    std::lock_guard<std::mutex> lock(activeListMutex);
    if (activeDecoders.empty()) {
        LOG_ERROR("[DecoderManager] ❌ AdvanceSubSequence called with no active decoders!");
        return false;
    }

    int nextSubSequence = currentSubSequence + 1;
    if (nextSubSequence > subSequenceCount){
        nextSubSequence = 1;
    }

    // Check if the next subsequence is loaded and decoded
    bool nextFound = false;
    for (int i = 0; i < activeDecoders.size(); i++){
        if (activeDecoders[i]->GetName() == std::to_string(nextSubSequence)){
            if (activeDecoders[i]->IsDecoded()){
                nextFound = true;
                break;
            }
            else{
                LOG_INFO("[DecoderManager] Next Sequence Not Decoded");
                return false;
            }
        }
    }

    if (!nextFound) {
        LOG_INFO("[DecoderManager] Next Sequence Not Found");
        return false;
    }
    LOG_INFO("Advancing subSequence!");
    currentSubSequence = nextSubSequence;
    // Clean up decoders
    auto it = activeDecoders.begin();
    while (it != activeDecoders.end()) {
        bool shouldKeep = false;
        int decoderNum = std::stoi((*it)->GetName());

        // Check if within preLoad window
        for(int y = 0; y < preLoad; y++){
            int checkNum = currentSubSequence + y;
            if (checkNum > subSequenceCount) checkNum -= subSequenceCount; // Handle wrap

            if (decoderNum == checkNum){
                shouldKeep = true;
                break;
            }
        }
        if(!shouldKeep){
            LOG_INFO("[DecoderManager] Removing decoder %d", decoderNum);
            it = activeDecoders.erase(it);
        }
        ++it;
    }
    return true;
}

void PlaybackManager::LoadSubSequence(int subSequence){
    if (subSequence > subSequenceCount){
        LOG_ERROR("[DecoderManager] ❌ SubSequence Out Of Range");
        return;
    }
    for (int i = 0; i < activeDecoders.size(); i++){
        if (activeDecoders[i]->GetName() == std::to_string(subSequence)){
            LOG_INFO("Sequence allready loaded");
            return;
        }
    }
    std::ostringstream oss;
    oss << "subsequence_" << std::setw(3) << std::setfill('0') << subSequence;
    std::filesystem::path subFolder = std::filesystem::path(sequenceDirectory)/(oss.str());
    auto newDecoder = std::make_shared<TVMDecoder::Decoder>(std::to_string(subSequence));
    {
        std::lock_guard<std::mutex> lock(activeListMutex);
        activeDecoders.push_back(newDecoder);
    }
    // Load seuqence
    LOG_INFO("[DecoderManager] Loading subsequence %d from %s", subSequence, subFolder.string().c_str());
    newDecoder->LoadSequence(subFolder);
}

void PlaybackManager::DecodeSubSequence(int subSequence){
    if (subSequence > subSequenceCount){
        LOG_ERROR("[DecoderManager] ❌ SubSequence Out Of Range");
    }
    for (int i = 0; i < activeDecoders.size(); i++){
        if (activeDecoders[i]->GetName() == std::to_string(subSequence)){
            LOG_INFO("[DecoderManager] ➡ Starting decode for subsequence %d...", subSequence);
            activeDecoders[i]->DecodeSequence();
            LOG_INFO("[DecoderManager] ✅ Finished decode for subsequence %d", subSequence);
            return;
        }
    }
    LOG_ERROR("[DecoderManager] Trying to decode unloaded sequence");

}

int PlaybackManager::getSubSequenceCount(){
    return subSequenceCount;
}

std::vector<Eigen::Vector3d, Eigen::aligned_allocator<Eigen::Vector3d>>
PlaybackManager::fetchFrame(int frame){
    std::lock_guard<std::mutex> lock(activeListMutex);  // ADD THIS!

    TVMLogger::LogInfo("fetchFrame called with frame: " + std::to_string(frame) +
                       ", current subsequence: " + std::to_string(currentSubSequence));

    if (activeDecoders.empty()) {
        throw std::runtime_error("No active decoders");
    }

    if (frame < 0 || frame >= activeDecoders.front()->GetTotalFrames()) {
        TVMLogger::LogError("Frame " + std::to_string(frame) + " out of range for decoder with " +
                            std::to_string(activeDecoders.front()->GetTotalFrames()) + " frames");
    }

    std::vector<Eigen::Vector3d, Eigen::aligned_allocator<Eigen::Vector3d>> frameData;
    for (auto& decoder : activeDecoders) {
        if (decoder->GetName() == std::to_string(currentSubSequence)) {
            frameData = decoder->ApplyDisplacementToFrame(frame);
            return frameData;
        }
    }
    LOG_ERROR("[DecoderManager] ❌ Fetch Frame called for non active subsequence");
    return frameData;
}

std::shared_ptr<TVMDecoder::Decoder>  PlaybackManager::getCurrentDecoder(){
    std::lock_guard<std::mutex> lock(activeListMutex); // ✅ Add lock
    for (auto& decoder : activeDecoders) {
        if (decoder->GetName() == std::to_string(currentSubSequence)) {
            return decoder;
        }
    }
    return nullptr;
}


