#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string_view>
namespace star::terrain {
constexpr std::uint64_t RangeBlockBytes=1024*1024;
constexpr std::uint64_t MaxRangeFileBytes=4ull*1024*1024*1024;
// TIFF's toff_t is unsigned, but CUR/END encode a signed relative displacement
// in it. Check both directions without signed conversion or unsigned wraparound.
inline bool RangeSeekPosition(std::uint64_t size,std::uint64_t position,
    std::uint64_t offset,int origin,std::uint64_t& result) {
    if(!size||size>MaxRangeFileBytes||position>size)return false;
    std::uint64_t next=offset;
    if(origin==SEEK_CUR||origin==SEEK_END) {
        const auto base=origin==SEEK_CUR?position:size;
        if(offset&(1ull<<63)) {
            const auto magnitude=~offset+1;
            if(magnitude>base)return false;
            next=base-magnitude;
        } else {
            if(offset>size-base)return false;
            next=base+offset;
        }
    } else if(origin!=SEEK_SET)return false;
    if(next>size)return false;
    result=next;return true;
}
inline bool CompleteRangeBlock(std::uint64_t size,std::uint64_t index,std::uint64_t bytes) {
    if(!size||size>MaxRangeFileBytes||index>(size-1)/RangeBlockBytes)return false;
    return bytes==std::min(RangeBlockBytes,size-index*RangeBlockBytes);
}

namespace detail {
inline void TrimRangeWhitespace(std::string_view& text) {
    while(!text.empty()&&(text.front()==' '||text.front()=='\t'||text.front()=='\r'||text.front()=='\n'))text.remove_prefix(1);
    while(!text.empty()&&(text.back()==' '||text.back()=='\t'||text.back()=='\r'||text.back()=='\n'))text.remove_suffix(1);
}
inline bool RangeNumber(std::string_view& text,std::uint64_t& result) {
    if(text.empty()||text.front()<'0'||text.front()>'9')return false;
    std::uint64_t n=0;
    while(!text.empty()&&text.front()>='0'&&text.front()<='9') {
        const auto digit=static_cast<std::uint64_t>(text.front()-'0');
        if(n>(MaxRangeFileBytes-digit)/10)return false;
        n=n*10+digit;text.remove_prefix(1);
    }
    result=n;return true;
}
}
inline bool ParseRangeFileSize(std::string_view text,std::uint64_t& size) {
    if(text.size()>128)return false;
    detail::TrimRangeWhitespace(text);std::uint64_t parsed=0;
    if(!detail::RangeNumber(text,parsed)||!text.empty()||!parsed)return false;
    size=parsed;return true;
}
// RFC 9110, section 14.4; require a complete block for this cache's policy.
// Do not publish a new total until the entire response has been checked.
inline bool ValidateRangeResponse(std::string_view header,std::uint64_t index,
    std::uint64_t bytes,std::uint64_t knownSize,std::uint64_t& total) {
    if(header.size()>128||index>=MaxRangeFileBytes/RangeBlockBytes)return false;
    detail::TrimRangeWhitespace(header);
    if(header.size()<6)return false;
    constexpr char unit[]="bytes ";
    for(std::size_t i=0;i<5;++i)if(header[i]!=unit[i]&&header[i]!=unit[i]-'a'+'A')return false;
    if(header[5]!=' ')return false;
    header.remove_prefix(6);std::uint64_t start=0,end=0,parsed=0;
    if(!detail::RangeNumber(header,start)||header.empty()||header.front()!='-')return false;
    header.remove_prefix(1);
    if(!detail::RangeNumber(header,end)||header.empty()||header.front()!='/')return false;
    header.remove_prefix(1);
    if(!detail::RangeNumber(header,parsed)||!header.empty()||!parsed||end<start||end>=parsed||
       start!=index*RangeBlockBytes||bytes!=end-start+1||
       (knownSize&&knownSize!=parsed)||!CompleteRangeBlock(parsed,index,bytes))return false;
    total=parsed;return true;
}
}
