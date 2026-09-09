#pragma once
#include <cstdint>

namespace star::terrain {
constexpr std::uint64_t MaxDecodedRasterBytes=256ull*1024*1024;
constexpr std::uint64_t MaxDecodedTileBytes=64ull*1024*1024;
// The runtime data model represents unsigned integers or IEEE float32 only.
// Signed, undefined and complex encodings cannot be silently reinterpreted.
inline bool SupportedRasterSampleFormat(std::uint16_t bits,std::uint16_t format) {
    return (format==1&&(bits==8||bits==16||bits==32)) || (format==3&&bits==32);
}
// Bound products before multiplying. These are allocation limits, not claims
// about the resolution or scientific accuracy of any source dataset.
inline bool BoundedRasterLayout(std::uint32_t width,std::uint32_t height,
    std::uint16_t channels,std::uint16_t bits,int maximumDimension,
    std::uint32_t tileWidth,std::uint32_t tileHeight,std::uint64_t tileBytes,
    std::uint64_t& sampleBytes) {
    if(maximumDimension<=0||!width||!height||width>static_cast<std::uint32_t>(maximumDimension)||
       height>static_cast<std::uint32_t>(maximumDimension)||!channels||channels>4||
       (bits!=8&&bits!=16&&bits!=32)||!tileWidth||!tileHeight||!tileBytes||tileBytes>MaxDecodedTileBytes) return false;
    const std::uint64_t stride=channels*(bits/8);
    if(width>MaxDecodedRasterBytes/stride||height>MaxDecodedRasterBytes/(width*stride)||
       tileWidth>MaxDecodedTileBytes/stride||tileHeight>MaxDecodedTileBytes/(tileWidth*stride)) return false;
    if(tileBytes<tileWidth*stride*tileHeight) return false;
    sampleBytes=width*stride*height;return true;
}
inline bool CompleteDecodedTile(std::int64_t decodedBytes,std::uint64_t expectedBytes) {
    return expectedBytes>0&&expectedBytes<=MaxDecodedTileBytes&&decodedBytes>=0&&
        static_cast<std::uint64_t>(decodedBytes)==expectedBytes;
}
}
