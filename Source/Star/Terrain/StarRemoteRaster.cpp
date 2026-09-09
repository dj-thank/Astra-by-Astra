#include "Terrain/StarRemoteRaster.h"
#include "Terrain/RangeCachePolicy.h"
#include "Runtime/StarDiagnostics.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"
THIRD_PARTY_INCLUDES_START
#include "tiffio.h"
THIRD_PARTY_INCLUDES_END

namespace {
struct FRangeReader
{
    FString Url,Directory;
    const std::atomic<bool>& Cancel;
    uint64 Position=0,Size=0;
    double Deadline=FPlatformTime::Seconds()+150;
    TMap<uint64,TArray<uint8>> Blocks;
    static constexpr uint64 BlockSize=star::terrain::RangeBlockBytes;
    bool Failed=false;
    bool Fetch(uint64 Index)
    {
        if(Cancel.load()||Failed||FPlatformTime::Seconds()>Deadline)return false;
        if(Blocks.Contains(Index))return true;
        const FString Path=Directory/TEXT("range-")+LexToString(Index)+TEXT(".bin");
        TArray<uint8> Bytes;
        if(IFileManager::Get().FileExists(*Path)&&FFileHelper::LoadFileToArray(Bytes,*Path)&&star::terrain::CompleteRangeBlock(Size,Index,Bytes.Num()))
        {Blocks.Add(Index,MoveTemp(Bytes));return true;}
        if(!Bytes.IsEmpty())StarDiagnostics::Event(TEXT("raster_cache_incomplete"),FString::Printf(TEXT("source=%s block=%llu bytes=%d; refetch"),*FPaths::GetCleanFilename(Directory),Index,Bytes.Num()));
        struct FResponse {std::atomic<bool> Done{false};int Code=0;TArray<uint8> Data;FString Range;};
        auto Response=MakeShared<FResponse,ESPMode::ThreadSafe>();
        auto Request=FHttpModule::Get().CreateRequest();
        Request->SetURL(Url);Request->SetVerb(TEXT("GET"));Request->SetTimeout(20);
        Request->SetHeader(TEXT("Range"),FString::Printf(TEXT("bytes=%llu-%llu"),Index*BlockSize,(Index+1)*BlockSize-1));
        Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
        Request->OnProcessRequestComplete().BindLambda([Response](FHttpRequestPtr,FHttpResponsePtr R,bool Ok){
            if(Ok&&R){Response->Code=R->GetResponseCode();Response->Data=R->GetContent();Response->Range=R->GetHeader(TEXT("Content-Range"));}
            Response->Done.store(true);
        });
        if(!Request->ProcessRequest())return false;
        while(!Response->Done.load())
        {
            if(Cancel.load()||FPlatformTime::Seconds()>Deadline){Request->CancelRequest();
                StarDiagnostics::Event(Cancel.load()?TEXT("raster_cancelled"):TEXT("raster_timeout"),FString::Printf(TEXT("source=%s block=%llu"),*FPaths::GetCleanFilename(Directory),Index));return false;}
            FPlatformProcess::Sleep(0.01f);
        }
        if(Response->Code!=206||Response->Data.IsEmpty()||Response->Data.Num()>BlockSize){
            StarDiagnostics::Event(TEXT("raster_http_failed"),FString::Printf(TEXT("source=%s block=%llu http=%d bytes=%d"),*FPaths::GetCleanFilename(Directory),Index,Response->Code,Response->Data.Num()));return false;}
        FString Prefix,Total;if(!Response->Range.Split(TEXT("/"),&Prefix,&Total))return false;
        if(!Prefix.StartsWith(FString::Printf(TEXT("bytes %llu-"),Index*BlockSize)))return false;
        Size=FCString::Strtoui64(*Total,nullptr,10);
        if(!star::terrain::CompleteRangeBlock(Size,Index,Response->Data.Num())){
            StarDiagnostics::Event(TEXT("raster_range_incomplete"),FString::Printf(TEXT("source=%s block=%llu bytes=%d total=%llu"),*FPaths::GetCleanFilename(Directory),Index,Response->Data.Num(),Size));return false;}
        IFileManager::Get().MakeDirectory(*Directory,true);
        FFileHelper::SaveArrayToFile(Response->Data,*Path);
        FFileHelper::SaveStringToFile(LexToString(Size),*(Directory/TEXT("size.txt")));
        FFileHelper::SaveStringToFile(Url,*(Directory/TEXT("source-url.txt")));
        Blocks.Add(Index,MoveTemp(Response->Data));return true;
    }
};
tmsize_t Read(thandle_t Handle,void* Destination,tmsize_t Count)
{
    auto& R=*static_cast<FRangeReader*>(Handle);tmsize_t Done=0;
    if(Count<0||Count>512ll*1024*1024)return 0;
    while(Done<Count&&R.Position<R.Size)
    {
        uint64 Block=R.Position/FRangeReader::BlockSize,Offset=R.Position%FRangeReader::BlockSize;
        if(!R.Fetch(Block)){R.Failed=true;break;}
        const auto& Data=R.Blocks[Block];
        if(Offset>=static_cast<uint64>(Data.Num()))break;
        const auto N=FMath::Min<int64>(Count-Done,Data.Num()-Offset);
        FMemory::Memcpy(static_cast<uint8*>(Destination)+Done,Data.GetData()+Offset,N);Done+=N;R.Position+=N;
    }
    return Done;
}
tmsize_t Write(thandle_t,void*,tmsize_t){return 0;}
toff_t Seek(thandle_t H,toff_t Offset,int Whence)
{
    auto& R=*static_cast<FRangeReader*>(H);
    R.Position=Whence==SEEK_SET?Offset:Whence==SEEK_CUR?R.Position+Offset:R.Size+Offset;
    return R.Position;
}
int Close(thandle_t){return 0;}
toff_t Size(thandle_t H){return static_cast<FRangeReader*>(H)->Size;}
int Map(thandle_t,void**,toff_t*){return 0;}
void Unmap(thandle_t,void*,toff_t){}
}

bool FStarRemoteRaster::Load(const FString& Url,int32 MaximumDimension,const FString& Cache,const std::atomic<bool>& Cancel)
{
    SourceUrl=Url;
    FRangeReader R{Url,Cache/FMD5::HashAnsiString(*Url),Cancel};
    FString SizeText;
    if(IFileManager::Get().FileExists(*(R.Directory/TEXT("size.txt")))&&FFileHelper::LoadFileToString(SizeText,*(R.Directory/TEXT("size.txt"))))R.Size=FCString::Strtoui64(*SizeText,nullptr,10);
    if(!R.Fetch(0)||!R.Size){Error=TEXT("HTTP range unavailable");return false;}
    TIFF* T=TIFFClientOpen("STAR observed raster","rm",&R,Read,Write,Seek,Close,Size,Map,Unmap);
    if(!T){Error=TEXT("Invalid TIFF");return false;}
    int Best=-1;uint32 BestWidth=0;uint16 Directory=0;
    do {
        uint32 W=0,H=0;TIFFGetField(T,TIFFTAG_IMAGEWIDTH,&W);TIFFGetField(T,TIFFTAG_IMAGELENGTH,&H);
        if(Directory==0){NativeWidth=W;NativeHeight=H;}
        if(W<=static_cast<uint32>(MaximumDimension)&&H<=static_cast<uint32>(MaximumDimension)&&W>BestWidth){Best=Directory;BestWidth=W;}
        ++Directory;
    }while(Directory<32&&!Cancel.load()&&TIFFReadDirectory(T));
    if(Best<0||!TIFFSetDirectory(T,Best)){TIFFClose(T);Error=TEXT("No bounded overview");return false;}
    uint32 W=0,H=0,TW=0,TH=0;uint16 C=1,B=0,Format=1,Planar=1;
    TIFFGetField(T,TIFFTAG_IMAGEWIDTH,&W);TIFFGetField(T,TIFFTAG_IMAGELENGTH,&H);
    TIFFGetFieldDefaulted(T,TIFFTAG_SAMPLESPERPIXEL,&C);TIFFGetFieldDefaulted(T,TIFFTAG_BITSPERSAMPLE,&B);
    TIFFGetFieldDefaulted(T,TIFFTAG_SAMPLEFORMAT,&Format);TIFFGetFieldDefaulted(T,TIFFTAG_PLANARCONFIG,&Planar);
    if(!TIFFIsTiled(T)||Planar!=PLANARCONFIG_CONTIG||C>4||(B!=8&&B!=16&&B!=32))
    {TIFFClose(T);Error=TEXT("Unsupported TIFF layout");return false;}
    TIFFGetField(T,TIFFTAG_TILEWIDTH,&TW);TIFFGetField(T,TIFFTAG_TILELENGTH,&TH);
    const int64 TileBytes=TIFFTileSize64(T),Stride=C*(B/8);
    if(!TW||!TH||TileBytes<=0||TileBytes>64*1024*1024){TIFFClose(T);return false;}
    TArray<uint8> Tile;Tile.SetNumUninitialized(TileBytes);Samples.SetNumZeroed(int64(W)*H*Stride);
    bool Ok=true;
    for(uint32 Y=0;Y<H&&Ok;Y+=TH)for(uint32 X=0;X<W&&Ok;X+=TW)
    {
        if(Cancel.load()||TIFFReadEncodedTile(T,TIFFComputeTile(T,X,Y,0,0),Tile.GetData(),TileBytes)<0){Ok=false;break;}
        for(uint32 Row=0;Row<FMath::Min(TH,H-Y);++Row)
            FMemory::Memcpy(Samples.GetData()+(int64(Y+Row)*W+X)*Stride,Tile.GetData()+int64(Row)*TW*Stride,FMath::Min(TW,W-X)*Stride);
    }
    TIFFClose(T);
    if(!Ok||R.Failed){Samples.Empty();Error=TEXT("Incomplete raster; retained global fallback");return false;}
    Width=W;Height=H;Channels=C;Bits=B;Floating=Format==SAMPLEFORMAT_IEEEFP;return true;
}
