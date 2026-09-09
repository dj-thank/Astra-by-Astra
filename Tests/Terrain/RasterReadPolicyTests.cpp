#include "Terrain/RangeCachePolicy.h"
#include "Terrain/RasterReadPolicy.h"
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

namespace {
int checks=0,failures=0;
void Check(bool ok,const char* name){++checks;if(!ok){++failures;std::cerr<<"FAIL: "<<name<<'\n';}}
void Ranges() {
    using namespace star::terrain;
    std::uint64_t total=17;
    Check(ValidateRangeResponse("bytes 0-1048575/1048577",0,RangeBlockBytes,0,total)&&total==1048577,"complete first block");
    Check(ValidateRangeResponse("bytes 1048576-1048576/1048577",1,1,total,total),"one-byte final block");
    Check(ValidateRangeResponse(" BYTES 0-0/1\t",0,1,0,total)&&total==1,"case-insensitive unit and outer whitespace");
    for(const char* bad:{"", "bytes 0-0/1048576", "bytes 1-1048576/1048576", "bytes 0-1048575/1048576junk",
        "bytes 0-1048575/*", "bytes 0-1048575/+1048576", "bytes 0-1048575/0", "bytes 0-1048575/4294967297",
        "bytes 0-18446744073709551616/1048576", "bytes 1048575-0/1048576", "items 0-1048575/1048576", "bytes 0 -1048575/1048576"}) {
        total=17;Check(!ValidateRangeResponse(bad,0,RangeBlockBytes,0,total)&&total==17,"malformed range leaves total unchanged");
    }
    Check(!ValidateRangeResponse("bytes 0-1048575/1048576",0,RangeBlockBytes,2097152,total),"changed total rejected");
    Check(!ValidateRangeResponse("bytes 0-1048575/1048576",0,RangeBlockBytes-1,0,total),"short body rejected");
    Check(!ValidateRangeResponse("bytes 0-1048575/1048576",std::numeric_limits<std::uint64_t>::max(),RangeBlockBytes,0,total),"overflowing block index rejected");
    for(const char* bad:{"-1","0","1048576junk","4294967297","18446744073709551616",""}) {
        total=17;Check(!ParseRangeFileSize(bad,total)&&total==17,"invalid cached size leaves total unchanged");
    }
    Check(ParseRangeFileSize("4294967296\r\n",total)&&total==MaxRangeFileBytes,"maximum cached size");
    // Deterministic property sweep of block boundaries; no network access.
    std::uint64_t seed=1234567;
    for(int i=0;i<10000;++i) {
        seed=seed*6364136223846793005ull+1;
        const auto size=1+seed%MaxRangeFileBytes;
        const auto index=(seed>>32)%((size-1)/RangeBlockBytes+1);
        const auto start=index*RangeBlockBytes,bytes=std::min(RangeBlockBytes,size-start);
        const auto header="bytes "+std::to_string(start)+"-"+std::to_string(start+bytes-1)+"/"+std::to_string(size);
        total=0;Check(ValidateRangeResponse(header,index,bytes,0,total)&&total==size,"generated valid range");
        total=17;Check(!ValidateRangeResponse(header,index,bytes-1,0,total)&&total==17,"generated truncation rejected atomically");
    }
}
void Layouts() {
    using namespace star::terrain;
    std::uint64_t bytes=17;
    Check(BoundedRasterLayout(1800,1800,1,32,1800,256,256,256*256*4,bytes)&&bytes==1800ull*1800*4,"DEM layout");
    Check(BoundedRasterLayout(9000,9000,1,8,9000,512,512,512*512,bytes)&&bytes==81000000,"land class layout remains supported");
    Check(BoundedRasterLayout(4096,4096,4,32,4096,256,256,256*256*16,bytes)&&bytes==MaxDecodedRasterBytes,"exact decoded allocation cap");
    for(int kind=0;kind<10;++kind) {
        std::uint32_t w=1024,h=1024,tw=256,th=256;std::uint16_t c=4,b=8;
        int limit=1024;std::uint64_t tile=256*256*4;
        if(kind==0)limit=-1;
        if(kind==1)w=0;
        if(kind==2)h=0;
        if(kind==3)c=0;
        if(kind==4)c=5;
        if(kind==5)b=64;
        if(kind==6)tw=0;
        if(kind==7){w=h=65535;limit=65535;}
        if(kind==8){tw=th=std::numeric_limits<std::uint32_t>::max();tile=MaxDecodedTileBytes;}
        if(kind==9)--tile;
        bytes=17;Check(!BoundedRasterLayout(w,h,c,b,limit,tw,th,tile,bytes)&&bytes==17,"invalid layout rejects before allocation");
    }
    Check(CompleteDecodedTile(4096,4096),"full decoded tile accepted");
    for(std::int64_t n:{-1,0,1,4095,4097})Check(!CompleteDecodedTile(n,4096),"short/invalid decoded tile rejected");
    Check(!CompleteDecodedTile(0,0),"zero tile rejected");
}
}
int main(){Ranges();Layouts();std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;}
