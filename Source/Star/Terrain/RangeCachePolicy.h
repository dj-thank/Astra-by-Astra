#pragma once
#include <algorithm>
#include <cstdint>
namespace star::terrain {
constexpr std::uint64_t RangeBlockBytes=1024*1024;
inline bool CompleteRangeBlock(std::uint64_t size,std::uint64_t index,std::uint64_t bytes) {
    if(!size||size>4ull*1024*1024*1024||index>(size-1)/RangeBlockBytes)return false;
    return bytes==std::min(RangeBlockBytes,size-index*RangeBlockBytes);
}
}
