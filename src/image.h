#pragma once

#include "common.h"

#include <memory>
#include <string>
#include <vector>

namespace zb {



struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> bgra;
    std::string source_url;
};



std::shared_ptr<Image> DecodeImage(const uint8_t* data, size_t len);

inline std::shared_ptr<Image> DecodeImage(const std::string& bytes) {
    return DecodeImage(reinterpret_cast<const uint8_t*>(bytes.data()),
                       bytes.size());
}

}  
