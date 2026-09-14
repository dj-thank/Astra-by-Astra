#include "Terrain/RangeCachePolicy.h"
#include <cstdint>
#include <iostream>
#include <limits>

namespace {
std::uint64_t Checks=0,Failures=0,Random=0x74fd28539ab104dcull;
void Check(bool condition,const char* message){++Checks;if(!condition){++Failures;std::cerr<<"FAIL "<<message<<'\n';}}
std::uint64_t Next(){Random^=Random<<13;Random^=Random>>7;Random^=Random<<17;return Random;}
}
int main(){
    using namespace star::terrain;
    constexpr auto Sentinel=std::numeric_limits<std::uint64_t>::max();
    // Independently compute small signed offsets using signed arithmetic. The
    // reader sees their unsigned libtiff representation, including negatives.
    for(int i=0;i<50000;++i){
        const auto size=Next()%(MaxRangeFileBytes+1);
        const auto position=Next()%(size+1);
        const std::int64_t delta=static_cast<std::int64_t>(Next()%(4*size+1))-2*static_cast<std::int64_t>(size);
        const int origin=i%3==0?SEEK_SET:i%3==1?SEEK_CUR:SEEK_END;
        const auto base=origin==SEEK_SET?0:origin==SEEK_CUR?position:size;
        const auto expected=static_cast<std::int64_t>(base)+delta;
        const bool wanted=expected>=0&&expected<=static_cast<std::int64_t>(size);
        auto result=Sentinel;
        const bool actual=RangeSeekPosition(size,position,static_cast<std::uint64_t>(delta),origin,result);
        Check(actual==wanted,"seek accepted/rejected incorrectly");
        Check(result==(wanted?static_cast<std::uint64_t>(expected):Sentinel),"seek result or failure atomicity");
    }
    for(const auto offset:{std::uint64_t{0},std::uint64_t{1},MaxRangeFileBytes,MaxRangeFileBytes+1,
                           std::uint64_t{1}<<63,(std::uint64_t{1}<<63)-1,Sentinel}){
        for(int origin:{SEEK_SET,SEEK_CUR,SEEK_END,-1,42}){
            std::uint64_t result=Sentinel;
            const bool ok=RangeSeekPosition(MaxRangeFileBytes,MaxRangeFileBytes,offset,origin,result);
            if(ok)Check(result<=MaxRangeFileBytes,"64-bit boundary escaped object");
            else Check(result==Sentinel,"64-bit failure changed output");
        }
    }
    std::uint64_t out=777;
    Check(!RangeSeekPosition(MaxRangeFileBytes+1,0,0,SEEK_SET,out)&&out==777,"oversized object accepted");
    Check(!RangeSeekPosition(10,11,0,SEEK_SET,out)&&out==777,"invalid existing cursor accepted");
    Check(!RangeSeekPosition(0,0,0,SEEK_SET,out)&&out==777,"unknown size seek accepted");
    Check(IdentityRangeEncoding(""),"absent content encoding rejected");
    Check(IdentityRangeEncoding(" identity\t"),"identity encoding rejected");
    Check(IdentityRangeEncoding("IdEnTiTy"),"case-insensitive identity rejected");
    for(const auto encoding:{"gzip","br","deflate","identity,gzip","identity, identity","identityx"})
        Check(!IdentityRangeEncoding(encoding),"encoded or ambiguous representation accepted");
    Check(MaxResidentRangeBlocks*RangeBlockBytes==32ull*1024*1024,"unexpected resident block budget");
    std::cout<<"RESULT "<<Checks<<" checks, "<<Failures<<" failures\n";
    return Failures?1:0;
}
