// Compiles the actual adapter against a minimal UE test double and real libtiff.
// This is not an Unreal build, HTTP backend test, renderer test or VR test.
#include "CoreMinimal.h"
#include "tiffio.h"
#include <limits>
#include <stdexcept>
#include <iostream>

namespace {
std::atomic<bool>* CancelAfterDecode=nullptr;
double AdvanceAfterDecode=0;
tmsize_t TestReadEncodedTile(TIFF* tiff,ttile_t tile,void* data,tmsize_t count){
    const auto result=TIFFReadEncodedTile(tiff,tile,data,count);
    if(CancelAfterDecode)CancelAfterDecode->store(true);
    RasterTest::Now+=AdvanceAfterDecode;
    return result;
}
}
#define TIFFReadEncodedTile TestReadEncodedTile
#include "Terrain/StarRemoteRaster.cpp"
#undef TIFFReadEncodedTile

namespace {
int Cases=0,Failures=0;
std::filesystem::path Root;
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void Reset(){
    CancelAfterDecode=nullptr;AdvanceAfterDecode=0;RasterTest::Server={};RasterTest::Now=0;
    RasterTest::ArrayCopies=0;RasterTest::ArrayReads.clear();RasterTest::StringReads.clear();
    RasterTest::WriteFiles=true;RasterTest::Requests=0;RasterTest::CancelledRequests=0;
    RasterTest::RequestStarts=true;RasterTest::RequestCompletes=true;
}
void Case(const char* name,const std::function<void()>& test){
    ++Cases;Reset();
    try{test();std::cout<<"PASS "<<name<<"\n";}
    catch(const std::exception& error){++Failures;std::cout<<"FAIL "<<name<<": "<<error.what()<<"\n";}
    CancelAfterDecode=nullptr;
}
FString Cache(const char* name){auto path=Root/name;std::filesystem::create_directories(path);return path.string();}
TArray<uint8> Bytes(const std::filesystem::path& path){TArray<uint8> bytes;Require(FFileHelper::LoadFileToArray(bytes,path.string().c_str()),"fixture read");return bytes;}
std::filesystem::path Fixture(const char* name,uint16 bits=8,uint16 format=SAMPLEFORMAT_UINT,uint16 orientation=ORIENTATION_TOPLEFT,uint16 compression=COMPRESSION_NONE,uint32 width=19,uint32 height=17,const char* mode="w"){
    auto path=Root/(std::string(name)+".tif");TIFF* tiff=TIFFOpen(path.string().c_str(),mode);Require(tiff,"fixture TIFF open");
    TIFFSetField(tiff,TIFFTAG_IMAGEWIDTH,width);TIFFSetField(tiff,TIFFTAG_IMAGELENGTH,height);
    TIFFSetField(tiff,TIFFTAG_SAMPLESPERPIXEL,1);TIFFSetField(tiff,TIFFTAG_BITSPERSAMPLE,bits);
    TIFFSetField(tiff,TIFFTAG_SAMPLEFORMAT,format);TIFFSetField(tiff,TIFFTAG_PLANARCONFIG,PLANARCONFIG_CONTIG);
    TIFFSetField(tiff,TIFFTAG_PHOTOMETRIC,PHOTOMETRIC_MINISBLACK);TIFFSetField(tiff,TIFFTAG_ORIENTATION,orientation);
    TIFFSetField(tiff,TIFFTAG_TILEWIDTH,16);TIFFSetField(tiff,TIFFTAG_TILELENGTH,16);TIFFSetField(tiff,TIFFTAG_COMPRESSION,compression);
    std::vector<uint8> tile(static_cast<std::size_t>(16*16*(bits/8)));
    for(uint32 y=0;y<height;y+=16)for(uint32 x=0;x<width;x+=16){
        for(std::size_t i=0;i<tile.size();++i)tile[i]=static_cast<uint8>((i+x+y)%251);
        Require(TIFFWriteEncodedTile(tiff,TIFFComputeTile(tiff,x,y,0,0),tile.data(),static_cast<tmsize_t>(tile.size()))>=0,"fixture tile write");
    }
    TIFFClose(tiff);return path;
}
FHttpResponsePtr Respond(const FHttpRequest& request,const TArray<uint8>& bytes){
    auto response=std::make_shared<FHttpResponse>();unsigned long long begin=0,end=0;
    auto range=request.Headers.find("Range");Require(range!=request.Headers.end(),"Range request missing");
    Require(std::sscanf(range->second.c_str(),"bytes=%llu-%llu",&begin,&end)==2,"Range request invalid");
    if(begin>=static_cast<uint64>(bytes.Num())){response->Code=416;return response;}
    end=std::min(end,static_cast<uint64>(bytes.Num()-1));
    response->Body.Values.assign(bytes.Values.begin()+begin,bytes.Values.begin()+end+1);
    response->Headers["Content-Range"]="bytes "+std::to_string(begin)+"-"+std::to_string(end)+"/"+std::to_string(bytes.Num());return response;
}
void Serve(TArray<uint8> bytes){RasterTest::Server=[bytes=std::move(bytes)](const FHttpRequest& request){return Respond(request,bytes);};}
FStarRemoteRaster Sentinel(){
    FStarRemoteRaster r;r.Width=3;r.Height=2;r.NativeWidth=30;r.NativeHeight=20;r.Channels=1;r.Bits=8;
    r.SourceUrl="previous";r.Samples.Values={1,2,3,4,5,6};return r;
}
void Preserved(const FStarRemoteRaster& r){
    Require(r.Width==3&&r.Height==2&&r.NativeWidth==30&&r.NativeHeight==20&&r.Channels==1&&r.Bits==8&&!r.Floating,"metadata changed on failure");
    Require(r.SourceUrl==FString("previous"),"source attribution changed on failure");
    Require(r.Samples.Values==std::vector<uint8>({1,2,3,4,5,6}),"previous sample buffer lost");
    Require(!r.Error.IsEmpty(),"failure did not report an error");
}
}
int main(int argc,char** argv){
    if(argc!=2){std::cerr<<"Usage: raster-integration FIXTURE_DIRECTORY\n";return 2;}
    Root=argv[1];std::filesystem::create_directories(Root);
    Case("valid cropped edge tiles",[]{
        auto path=Fixture("valid");Serve(Bytes(path));std::atomic<bool> cancel{false};FStarRemoteRaster r;
        Require(r.Load("valid",64,Cache("valid-cache"),cancel),"valid TIFF rejected");
        Require(r.Width==19&&r.Height==17&&r.NativeWidth==19&&r.NativeHeight==17,"wrong dimensions");
        Require(r.Samples.Num()==19*17,"wrong sample buffer size");
        for(int y=0;y<17;++y)for(int x=0;x<19;++x)Require(r.Samples[y*19+x]==static_cast<uint8>(((y%16)*16+x%16+(x/16)*16+(y/16)*16)%251),"edge tile stride/crop mismatch");
    });
    Case("HTTP failure preserves previous raster",[]{std::atomic<bool> cancel{false};auto r=Sentinel();Require(!r.Load("unavailable",64,Cache("http-failure"),cancel),"HTTP failure accepted");Preserved(r);});
    Case("invalid TIFF preserves previous raster",[]{TArray<uint8> bytes;bytes.Values.assign(100,42);Serve(std::move(bytes));std::atomic<bool> cancel{false};auto r=Sentinel();Require(!r.Load("invalid",64,Cache("invalid-tiff"),cancel),"invalid TIFF accepted");Preserved(r);});
    Case("missing bounded overview preserves native metadata",[]{Serve(Bytes(Fixture("overview")));std::atomic<bool> cancel{false};auto r=Sentinel();Require(!r.Load("overview",8,Cache("overview-cache"),cancel),"oversized overview accepted");Preserved(r);});
    Case("corrupt compressed tile preserves previous samples",[]{
        auto path=Fixture("corrupt",8,SAMPLEFORMAT_UINT,ORIENTATION_TOPLEFT,COMPRESSION_ADOBE_DEFLATE);
        TIFF* tiff=TIFFOpen(path.string().c_str(),"r");uint64_t* offsets=nullptr;Require(TIFFGetField(tiff,TIFFTAG_TILEOFFSETS,&offsets),"missing tile offsets");auto offset=offsets[0];TIFFClose(tiff);
        auto bytes=Bytes(path);for(int i=0;i<8;++i)bytes.Values.at(static_cast<std::size_t>(offset+i))=255;Serve(std::move(bytes));
        std::atomic<bool> cancel{false};auto r=Sentinel();Require(!r.Load("corrupt",64,Cache("corrupt-cache"),cancel),"corrupt tile accepted");Preserved(r);
    });
    Case("successful retry clears old error",[]{Serve(Bytes(Fixture("retry")));std::atomic<bool> cancel{false};auto r=Sentinel();r.Error="old network failure";Require(r.Load("retry",64,Cache("retry-cache"),cancel),"retry failed");Require(r.Error.IsEmpty(),"stale error after success");Require(r.SourceUrl==FString("retry"),"source not committed");});
    Case("cancel after final decoded tile prevents commit",[]{Serve(Bytes(Fixture("cancel-final",8,SAMPLEFORMAT_UINT,ORIENTATION_TOPLEFT,COMPRESSION_NONE,16,16)));std::atomic<bool> cancel{false};CancelAfterDecode=&cancel;auto r=Sentinel();Require(!r.Load("cancel-final",64,Cache("cancel-final-cache"),cancel),"cancelled raster was committed");Preserved(r);});
    Case("signed samples must not be reinterpreted as unsigned",[]{Serve(Bytes(Fixture("signed",16,SAMPLEFORMAT_INT)));std::atomic<bool> cancel{false};auto r=Sentinel();Require(!r.Load("signed",64,Cache("signed-cache"),cancel),"signed TIFF accepted without signed metadata");Preserved(r);});
    Case("unsupported floating bit width rejected",[]{Serve(Bytes(Fixture("float16",16,SAMPLEFORMAT_IEEEFP)));std::atomic<bool> cancel{false};auto r=Sentinel();Require(!r.Load("float16",64,Cache("float16-cache"),cancel),"float16 was advertised as usable numeric data");Preserved(r);});
    Case("non-top-left image cannot silently invert geography",[]{Serve(Bytes(Fixture("orientation",8,SAMPLEFORMAT_UINT,ORIENTATION_BOTLEFT)));std::atomic<bool> cancel{false};auto r=Sentinel();Require(!r.Load("orientation",64,Cache("orientation-cache"),cancel),"unsupported orientation accepted unchanged");Preserved(r);});
    Case("oversized cached block is rejected before allocation",[]{
        auto directory=Cache("oversized-block");TArray<uint8> huge;huge.Values.assign(2*1024*1024,0);
        auto path=directory/"range-0.bin";Require(FFileHelper::SaveArrayToFile(huge,*path),"cache fixture write");
        std::atomic<bool> cancel{false};FRangeReader r{"oversized",directory,cancel};r.Size=100;Require(!r.Fetch(0),"bad cache accepted");
        Require(RasterTest::ArrayReads.empty(),"oversized cached block was loaded into memory before validation");
    });
    Case("oversized size metadata is rejected before loading",[]{
        auto cache=Cache("oversized-metadata");auto directory=cache/FMD5::HashAnsiString("meta");std::filesystem::create_directories(directory.Value);
        auto path=directory/"size.txt";Require(FFileHelper::SaveStringToFile(FString(std::string(1024,'1')),*path),"metadata fixture write");
        std::atomic<bool> cancel{false};FStarRemoteRaster r;Require(!r.Load("meta",64,cache,cancel),"bad metadata accepted");
        Require(RasterTest::StringReads.empty(),"oversized size metadata loaded before validation");
    });
    Case("large HTTP body is not copied a second time",[]{
        RasterTest::Server=[](const FHttpRequest&){auto response=std::make_shared<FHttpResponse>();response->Body.Values.assign(2*1024*1024,0);return response;};
        std::atomic<bool> cancel{false};FRangeReader r{"oversized-http",Cache("http-body"),cancel};Require(!r.Fetch(0),"oversized HTTP body accepted");
        Require(RasterTest::ArrayCopies==0,"oversized HTTP body copied before size validation");
    });
    Case("request asks for identity content encoding",[]{
        RasterTest::Server=[](const FHttpRequest& request){auto it=request.Headers.find("Accept-Encoding");Require(it!=request.Headers.end()&&it->second=="identity","identity content encoding not requested");return FHttpResponsePtr{};};
        std::atomic<bool> cancel{false};FRangeReader r{"identity",Cache("identity"),cancel};Require(!r.Fetch(0),"empty response accepted");
    });
    Case("encoded response cannot populate a byte range cache",[]{
        RasterTest::Server=[](const FHttpRequest&){auto response=std::make_shared<FHttpResponse>();response->Body.Values.assign(100,0);response->Headers["Content-Range"]="bytes 0-99/100";response->Headers["Content-Encoding"]="gzip";return response;};
        std::atomic<bool> cancel{false};FRangeReader r{"encoded",Cache("encoded"),cancel};Require(!r.Fetch(0),"encoded representation accepted as raw byte offsets");Require(r.Size==0&&r.Blocks.Num()==0,"invalid representation published");
    });
    Case("invalid seek origin fails without changing position",[]{std::atomic<bool> cancel{false};FRangeReader r{"seek","",cancel};r.Size=100;r.Position=10;Require(Seek(&r,1,12345)==static_cast<toff_t>(-1),"invalid whence accepted");Require(r.Position==10&&r.Failed,"seek failure was not atomic/sticky");});
    Case("relative seek underflow cannot wrap",[]{std::atomic<bool> cancel{false};FRangeReader r{"seek","",cancel};r.Size=100;r.Position=5;Require(Seek(&r,static_cast<toff_t>(-6),SEEK_CUR)==static_cast<toff_t>(-1),"underflow accepted");Require(r.Position==5&&r.Failed,"underflow changed reader position");});
    Case("seek beyond bounded object fails",[]{std::atomic<bool> cancel{false};FRangeReader r{"seek","",cancel};r.Size=100;r.Position=5;Require(Seek(&r,101,SEEK_SET)==static_cast<toff_t>(-1),"out-of-bounds offset accepted");Require(r.Position==5&&r.Failed,"out-of-bounds seek changed position");});
    Case("valid backward seek and EOF remain supported",[]{std::atomic<bool> cancel{false};FRangeReader r{"seek","",cancel};r.Size=100;r.Position=10;Require(Seek(&r,static_cast<toff_t>(-3),SEEK_CUR)==7,"valid relative backward seek rejected");Require(Seek(&r,static_cast<toff_t>(-4),SEEK_END)==96,"valid end-relative seek rejected");Require(Seek(&r,100,SEEK_SET)==100&&!r.Failed,"EOF seek rejected");});
    Case("invalid read length latches failure",[]{std::atomic<bool> cancel{false};FRangeReader r{"read","",cancel};r.Size=100;uint8 destination=0;Require(Read(&r,&destination,-1)==0&&r.Failed,"negative read length silently ignored");});
    Case("out-of-range block is rejected before HTTP",[]{std::atomic<bool> cancel{false};FRangeReader r{"range",Cache("bad-index"),cancel};Require(!r.Fetch(std::numeric_limits<uint64>::max()),"invalid block fetched");Require(RasterTest::Requests==0,"overflowing block index reached HTTP");});
    Case("resident range cache has a fixed memory budget",[]{
        RasterTest::WriteFiles=false;
        RasterTest::Server=[](const FHttpRequest& request){unsigned long long begin=0,end=0;std::sscanf(request.Headers.at("Range").c_str(),"bytes=%llu-%llu",&begin,&end);auto response=std::make_shared<FHttpResponse>();response->Body.Values.assign(1024*1024,0);response->Headers["Content-Range"]="bytes "+std::to_string(begin)+"-"+std::to_string(end)+"/41943040";return response;};
        std::atomic<bool> cancel{false};FRangeReader r{"many-blocks",Cache("many-blocks"),cancel};r.Size=40ull*1024*1024;
        for(uint64 i=0;i<40;++i)Require(r.Fetch(i),"valid sequential block rejected");
        Require(r.Blocks.Num()<=32,"range reader retains more than 32 MiB of compressed blocks");
    });
    Case("cached valid TIFF works without HTTP",[]{
        auto cache=Cache("offline-valid");Serve(Bytes(Fixture("offline")));std::atomic<bool> cancel{false};FStarRemoteRaster first;Require(first.Load("offline",64,cache,cancel),"initial fetch failed");RasterTest::Server={};RasterTest::Requests=0;FStarRemoteRaster second;Require(second.Load("offline",64,cache,cancel),"valid cache failed offline");Require(RasterTest::Requests==0&&second.Samples.Values==first.Samples.Values,"offline cache mismatch");
    });
    Case("request start failure is reported",[]{RasterTest::RequestStarts=false;std::atomic<bool> cancel{false};FRangeReader r{"start",Cache("start-fail"),cancel};Require(!r.Fetch(0),"failed request start accepted");});
    Case("deadline cancels an unfinished request",[]{RasterTest::RequestCompletes=false;std::atomic<bool> cancel{false};FRangeReader r{"timeout",Cache("timeout"),cancel};r.Deadline=0.02;Require(!r.Fetch(0),"request deadline ignored");Require(RasterTest::CancelledRequests==1,"timed out request not cancelled");});
    for(uint16 bits:{uint16(8),uint16(16),uint16(32)})Case(("valid unsigned "+std::to_string(bits)).c_str(),[bits]{auto name="unsigned-"+std::to_string(bits);Serve(Bytes(Fixture(name.c_str(),bits)));std::atomic<bool> cancel{false};FStarRemoteRaster r;Require(r.Load(name.c_str(),64,Cache((name+"-cache").c_str()),cancel),"valid unsigned image rejected");Require(r.Bits==bits&&!r.Floating,"unsigned type metadata wrong");});
    Case("valid float32",[]{Serve(Bytes(Fixture("float32",32,SAMPLEFORMAT_IEEEFP)));std::atomic<bool> cancel{false};FStarRemoteRaster r;Require(r.Load("float32",64,Cache("float32-cache"),cancel)&&r.Floating&&r.Bits==32,"valid float32 rejected");});
    Case("big-endian TIFF is decoded",[]{Serve(Bytes(Fixture("big-endian",16,SAMPLEFORMAT_UINT,ORIENTATION_TOPLEFT,COMPRESSION_NONE,19,17,"wb")));std::atomic<bool> cancel{false};FStarRemoteRaster r;Require(r.Load("big-endian",64,Cache("big-endian-cache"),cancel),"big-endian fixture rejected");uint16 sample=0;std::memcpy(&sample,r.Samples.GetData(),2);uint8 expectedBytes[2]={0,1};uint16 expected=0;std::memcpy(&expected,expectedBytes,2);Require(sample==expected,"byte order conversion wrong");});

    Case("pre-cancelled load preserves prior source and pixels",[]{std::atomic<bool> cancel{true};auto r=Sentinel();Require(!r.Load("cancelled",64,Cache("pre-cancel"),cancel),"pre-cancelled load succeeded");Preserved(r);Require(RasterTest::Requests==0,"pre-cancelled load made HTTP request");});
    Case("cancelled HTTP completion cannot publish a cache block",[]{
        std::atomic<bool> cancel{false};
        RasterTest::Server=[&cancel](const FHttpRequest&){auto response=std::make_shared<FHttpResponse>();response->Body.Values.assign(100,1);response->Headers["Content-Range"]="bytes 0-99/100";cancel.store(true);return response;};
        FRangeReader r{"cancel-response",Cache("cancel-response"),cancel};Require(!r.Fetch(0),"cancelled HTTP response was published");Require(r.Size==0&&r.Blocks.Num()==0,"cancelled response mutated cache state");
    });
    Case("deadline after final decoded tile prevents commit",[]{Serve(Bytes(Fixture("deadline-final",8,SAMPLEFORMAT_UINT,ORIENTATION_TOPLEFT,COMPRESSION_NONE,16,16)));AdvanceAfterDecode=151;std::atomic<bool> cancel{false};auto r=Sentinel();Require(!r.Load("deadline-final",64,Cache("deadline-final-cache"),cancel),"over-deadline raster committed");Preserved(r);});
    Case("empty-file null destination cannot be silently accepted",[]{std::atomic<bool> cancel{false};FRangeReader r{"read","",cancel};Require(Read(&r,nullptr,1)==0&&r.Failed,"null destination did not latch failure");});
    Case("excessive read length latches failure without allocation",[]{std::atomic<bool> cancel{false};FRangeReader r{"read","",cancel};uint8 target=0;Require(Read(&r,&target,512ll*1024*1024+1)==0&&r.Failed,"oversized read length ignored");});
    Case("zero-length read and regular EOF remain valid",[]{
        std::atomic<bool> cancel{false};FRangeReader r{"read","",cancel};r.Size=100;TArray<uint8> block;block.Values.assign(100,73);r.Blocks.Add(0,MoveTemp(block));
        Require(Read(&r,nullptr,0)==0&&!r.Failed,"zero-length read rejected");uint8 target[200]={};Require(Read(&r,target,200)==100&&!r.Failed&&r.Position==100,"normal EOF mishandled");
        for(int i=0;i<100;++i)Require(target[i]==73,"read content mismatch");
    });
    Case("unexpected resident block truncation latches failure",[]{std::atomic<bool> cancel{false};FRangeReader r{"read","",cancel};r.Size=100;TArray<uint8> block;block.Values.assign(10,2);r.Blocks.Add(0,MoveTemp(block));uint8 target[20]={};Require(Read(&r,target,20)==10&&r.Failed,"short resident block was silently treated as EOF");});
    Case("known object size prevents out-of-range block HTTP",[]{std::atomic<bool> cancel{false};FRangeReader r{"outside",Cache("outside"),cancel};r.Size=100;Require(!r.Fetch(1),"block beyond file fetched");Require(RasterTest::Requests==0,"block beyond known file reached HTTP");});
    Case("truncated disk block is refetched",[]{
        auto directory=Cache("truncated-block");TArray<uint8> shortBlock;shortBlock.Values.assign(10,0);auto path=directory/"range-0.bin";Require(FFileHelper::SaveArrayToFile(shortBlock,*path),"fixture write");
        TArray<uint8> valid;valid.Values.assign(100,91);Serve(std::move(valid));std::atomic<bool> cancel{false};FRangeReader r{"truncated",directory,cancel};r.Size=100;
        Require(r.Fetch(0)&&r.Blocks[0].Num()==100&&r.Blocks[0][0]==91,"truncated cache did not recover");Require(RasterTest::Requests==1,"truncated block refetch count");
    });
    Case("HTTP known-size mismatch cannot populate cache",[]{
        TArray<uint8> data;data.Values.assign(100,0);Serve(std::move(data));std::atomic<bool> cancel{false};FRangeReader r{"mismatch",Cache("mismatch"),cancel};r.Size=101;
        Require(!r.Fetch(0)&&r.Size==101&&r.Blocks.Num()==0,"known-size mismatch accepted or state changed");
    });
    Case("HTTP 200 ignoring range cannot populate cache",[]{
        RasterTest::Server=[](const FHttpRequest&){auto response=std::make_shared<FHttpResponse>();response->Code=200;response->Body.Values.assign(100,0);return response;};
        std::atomic<bool> cancel{false};FRangeReader r{"ignored",Cache("ignored"),cancel};Require(!r.Fetch(0)&&r.Size==0&&r.Blocks.Num()==0,"HTTP 200 accepted as partial object");
    });
    Case("disk write failures do not corrupt usable response data",[]{
        RasterTest::WriteFiles=false;Serve(Bytes(Fixture("disk-full")));std::atomic<bool> cancel{false};FStarRemoteRaster r;Require(r.Load("disk-full",64,Cache("disk-full-cache"),cancel),"optional cache failure blocked valid raster");Require(r.Samples.Num()==19*17,"data lost after cache write failure");
    });
    Case("zero dimension limit preserves prior raster",[]{std::atomic<bool> cancel{false};auto r=Sentinel();Require(!r.Load("bad-dimension",0,Cache("bad-dimension"),cancel),"zero dimension accepted");Preserved(r);});
    std::cout<<"RESULT "<<Cases<<" cases, "<<Failures<<" failures\n";return Failures?1:0;
}
