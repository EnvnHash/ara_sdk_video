//
// Created by sven on 06-08-25.
//

#include "Sample.h"

using namespace std;

namespace ara::av {

Sample::Sample(const std::filesystem::path& p) {
    load(p);
}

void Sample::load(const std::filesystem::path& p) {
    try {
        if (filesystem::exists(p)) {
            if (p.extension() == ".wav") {
                m_audioFile = make_unique<AudioFileWav>();
            } else if (p.extension() == ".aif"){
                m_audioFile = make_unique<AudioFileAiff>();
            } else {
                throw("unsupported file type");
            }
            m_audioFile->load(p.string(), SampleOrder::Interleaved);
        } else {
            throw("File doesn't exist");
        }
    } catch(const std::runtime_error& err) {
        LOGE << err.what();
    }
}

}