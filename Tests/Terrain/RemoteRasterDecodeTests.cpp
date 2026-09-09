// The .inc contains the actual production Load and Seek methods. Real libtiff
// decodes locally generated TIFF bytes; these small UE/transport host adapters
// do not test HTTP, Unreal allocation, game threads, rendering or real DEM data.
#define TIFF_DISABLE_DEPRECATED
#include <tiffio.h>
#include "Terrain/RangeCachePolicy.h"
#include "Terrain/RasterReadPolicy.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
using uint8=std::uint8_t;using uint16=std::uint16_t;using uint32=std::uint32_t;
using uint64=std::uint64_t;using int32=std::int32_t;using int64=std::int64_t;
#define TEXT(s) s
#define TCHAR_TO_UTF8(s) s
#define MoveTemp(s) std::move(s)
struct FString {
    std::string value;
    FString()=default;FString(const char* s):value(s){} FString(std::string s):value(std::move(s)){}
    const char* operator*() const{return value.c_str();}
    void Empty(){value.clear();}
    friend FString operator/(const FString& a,const FString& b){return a.value+"/"+b.value;}
};
template<class T> struct TArray {
    std::vector<T> values;
    void SetNumUninitialized(int32 n){values.resize(static_cast<std::size_t>(n));}
    void SetNumZeroed(int32 n){values.assign(static_cast<std::size_t>(n),T{});}
    T* GetData(){return values.data();}
    void Empty(){values.clear();}
};
struct FMD5{static FString HashAnsiString(const char*){return "host-cache-disabled";}};
struct IFileManager{static IFileManager& Get(){static IFileManager f;return f;}bool FileExists(const char*){return false;}};
namespace FFileHelper{bool LoadFileToString(FString&,const char*){return false;}}
namespace FMath{template<class T>T Min(T a,T b){return std::min(a,b);}}
namespace FMemory{void Memcpy(void* dst,const void* src,int64 n){std::memcpy(dst,src,static_cast<std::size_t>(n));}}
// Injectable read failure and cancellation occur inside the real decoder's I/O.
std::int64_t failFromOffset=-1,cancelFromOffset=-1;
int injectedFailures=0,injectedCancels=0;
std::atomic<bool>* cancelTarget=nullptr;
struct FRangeReader {
    FString Url,Directory;const std::atomic<bool>& Cancel;
    uint64 Position=0,Size=0;bool Failed=false;std::vector<uint8> bytes{};
    FRangeReader(FString url,FString directory,const std::atomic<bool>& cancel):Url(std::move(url)),Directory(std::move(directory)),Cancel(cancel){}
    bool Fetch(uint64){
        if(Cancel.load())return false;
        std::ifstream in(Url.value,std::ios::binary);
        if(!in)return false;
        bytes.assign(std::istreambuf_iterator<char>(in),{});Size=bytes.size();return Size!=0;
    }
};
tmsize_t Read(thandle_t h,void* to,tmsize_t n){
    auto& r=*static_cast<FRangeReader*>(h);
    if(n<0||r.Cancel.load()||r.Position>r.Size)return 0;
    if(failFromOffset>=0&&r.Position==static_cast<uint64>(failFromOffset)){r.Failed=true;++injectedFailures;return 0;}
    const auto count=std::min(static_cast<uint64>(n),r.Size-r.Position);
    std::memcpy(to,r.bytes.data()+r.Position,static_cast<std::size_t>(count));
    if(cancelFromOffset>=0&&r.Position==static_cast<uint64>(cancelFromOffset)&&cancelTarget){cancelTarget->store(true);++injectedCancels;}
    r.Position+=count;return static_cast<tmsize_t>(count);
}
tmsize_t Write(thandle_t,void*,tmsize_t){return 0;}
int Close(thandle_t){return 0;}
toff_t Size(thandle_t h){return static_cast<FRangeReader*>(h)->Size;}
int Map(thandle_t,void**,toff_t*){return 0;}
void Unmap(thandle_t,void*,toff_t){}
struct FStarRemoteRaster {
    int32 Width=0,Height=0,NativeWidth=0,NativeHeight=0,Channels=0,Bits=0;
    bool Floating=false;TArray<uint8> Samples;FString SourceUrl,Error;
    bool Load(const FString&,int32,const FString&,const std::atomic<bool>&);
};
#include "raster-load-method.inc"
namespace {
int checks=0,failures=0;
void Check(bool ok,const char* name){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<name<<'\n';}}
uint64 Fixture(const std::filesystem::path& path,uint16 bits,uint16 format,uint32 width=19,uint32 height=17){
    TIFF* t=TIFFOpen(path.string().c_str(),"w");if(!t)throw std::runtime_error("fixture create");
    TIFFSetField(t,TIFFTAG_IMAGEWIDTH,width);TIFFSetField(t,TIFFTAG_IMAGELENGTH,height);
    TIFFSetField(t,TIFFTAG_SAMPLESPERPIXEL,1);TIFFSetField(t,TIFFTAG_BITSPERSAMPLE,bits);
    TIFFSetField(t,TIFFTAG_SAMPLEFORMAT,format);TIFFSetField(t,TIFFTAG_PLANARCONFIG,PLANARCONFIG_CONTIG);
    TIFFSetField(t,TIFFTAG_PHOTOMETRIC,PHOTOMETRIC_MINISBLACK);TIFFSetField(t,TIFFTAG_COMPRESSION,COMPRESSION_NONE);
    TIFFSetField(t,TIFFTAG_TILEWIDTH,16);TIFFSetField(t,TIFFTAG_TILELENGTH,16);
    std::vector<uint8> tile(static_cast<std::size_t>(TIFFTileSize64(t)),42);
    for(uint32 y=0;y<height;y+=16)for(uint32 x=0;x<width;x+=16)
        if(TIFFWriteEncodedTile(t,TIFFComputeTile(t,x,y,0,0),tile.data(),static_cast<tmsize_t>(tile.size()))<0)throw std::runtime_error("fixture tile write");
    TIFFClose(t);t=TIFFOpen(path.string().c_str(),"r");if(!t)throw std::runtime_error("fixture reopen");
    uint64* offsets=nullptr;TIFFGetField(t,TIFFTAG_TILEOFFSETS,&offsets);
    const auto offset=offsets[TIFFNumberOfTiles(t)-1];TIFFClose(t);return offset;
}
bool SamePayload(const FStarRemoteRaster& a,const FStarRemoteRaster& b){
    return a.Width==b.Width&&a.Height==b.Height&&a.NativeWidth==b.NativeWidth&&a.NativeHeight==b.NativeHeight&&
        a.Channels==b.Channels&&a.Bits==b.Bits&&a.Floating==b.Floating&&a.Samples.values==b.Samples.values&&a.SourceUrl.value==b.SourceUrl.value;
}
}
int main(){
 try {
    std::atomic<bool> cancel{false};cancelTarget=&cancel;
    const auto directory=std::filesystem::current_path()/"raster-fixtures";std::filesystem::create_directories(directory);
    const auto good=directory/"uint8.tif",bad=directory/"int16.tif",other=directory/"other.tif",fp=directory/"float32.tif";
    const auto tileOffset=Fixture(good,8,SAMPLEFORMAT_UINT);
    Fixture(bad,16,SAMPLEFORMAT_INT);const auto lastTile=Fixture(other,8,SAMPLEFORMAT_UINT,33,19);Fixture(fp,32,SAMPLEFORMAT_IEEEFP);
    FStarRemoteRaster raster;auto load=[&](const auto& file,int limit=64){return raster.Load(FString(file.string()),limit,"unused",cancel);};
    Check(load(good),"unsigned fixture decodes with real libtiff");
    Check(raster.Width==19&&raster.Height==17&&raster.NativeWidth==19&&raster.NativeHeight==17&&raster.Bits==8&&!raster.Floating,"metadata");
    Check(raster.Samples.values.size()==19*17&&std::all_of(raster.Samples.values.begin(),raster.Samples.values.end(),[](auto b){return b==42;}),"partial edge tiles decoded and copied exactly");
    auto before=raster;
    Check(!load(bad),"signed samples unsupported rather than reinterpreted as unsigned");
    Check(SamePayload(raster,before),"unsupported sample format preserves old raster");
    Check(load(good),"recover good fixture");
    Check(raster.Error.value.empty(),"successful load clears previous error");
    before=raster;
    Check(!load(other,4),"missing bounded overview rejected");
    Check(SamePayload(raster,before),"failed overview retains native metadata and source URL");
    Check(load(good),"recover after overview failure");
    Check(raster.Error.value.empty(),"overview error cleared on recovery");
    // Fail only at the last tile's exact offset, after directory parsing and
    // decoding earlier tiles have succeeded (not during TIFF header opening).
    Check(load(other),"second dimensions decode normally");
    Check(load(good),"restore baseline before injected failure");before=raster;
    failFromOffset=static_cast<int64>(lastTile);
    Check(!load(other),"injected reader failure rejected");
    Check(injectedFailures==1,"injected failure reached tile decoding");
    Check(SamePayload(raster,before),"read failure retains old raster");failFromOffset=-1;
    cancel.store(true);Check(!load(other),"cancel before opening rejected");Check(SamePayload(raster,before),"cancellation retains source metadata");cancel.store(false);
    Check(load(good),"recover after transport cancellation");Check(raster.Error.value.empty(),"transport error cleared");
    before=raster;cancelFromOffset=static_cast<int64>(lastTile);
    Check(!load(other),"cancellation during last tile cannot commit new raster");
    Check(injectedCancels==1&&cancel.load(),"cancellation injected inside last tile read");
    Check(SamePayload(raster,before),"late cancellation retains old raster");
    cancelFromOffset=-1;cancel.store(false);
    Check(load(fp)&&raster.Floating&&raster.Bits==32,"IEEE float32 remains supported");
    before=raster;Check(!load(good,0)&&SamePayload(raster,before),"invalid limit retains raster");
    FRangeReader reader{"unused","unused",cancel};reader.Size=100;reader.Position=10;
    Check(Seek(&reader,20,SEEK_SET)==20,"absolute seek");
    Check(Seek(&reader,static_cast<toff_t>(-3),SEEK_CUR)==17,"negative relative seek");
    Check(Seek(&reader,static_cast<toff_t>(-1),SEEK_END)==99,"negative end seek");
    for(const auto origin:{SEEK_SET,SEEK_CUR,SEEK_END,77}){
        reader.Position=17;
        Check(Seek(&reader,200,origin)==static_cast<toff_t>(-1)&&reader.Position==17,"invalid seek fails without moving cursor");
    }
    reader.Position=1;
    Check(Seek(&reader,static_cast<toff_t>(-2),SEEK_CUR)==static_cast<toff_t>(-1)&&reader.Position==1,"relative underflow rejected");
    (void)tileOffset;
    std::cout<<checks<<" real-libtiff/extracted-method checks; failures="<<failures<<" (no HTTP/Unreal/device claim)\n";
    return failures?1:0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
