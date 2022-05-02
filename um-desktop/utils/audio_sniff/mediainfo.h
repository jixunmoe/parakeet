#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace umd::utils {

std::string audio_sniff_mediainfo(const uint8_t* buf, size_t len);

}  // namespace umd::utils