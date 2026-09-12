#include "Terrain/StarRemoteRaster.h"
#include "Terrain/RangeCachePolicy.h"
#include "Terrain/RasterReadPolicy.h"
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
    TArray<uint64> BlockOrder;
    static constexpr uint64 BlockSize=star::terrain::RangeBlockBytes;
    bool Failed=false;
    void RememberBlock(uint64 Index,TArray<uint8>&& Bytes)
    {
        // A source can be much larger than the selected overview. Keep only a
        // bounded FIFO of compressed blocks; evicted blocks remain on disk.
        if(Blocks.Num()>=static_cast<int32>(star::terrain::MaxResidentRangeBlocks)) {
            Blocks.Remove(BlockOrder[0]);BlockOrder.RemoveAt(0);
        }
        BlockOrder.Add(Index);Blocks.Add(Index,MoveTemp(Bytes));
    }
    bool Fetch(uint64 Index)
    {
        if(Cancel.load()||Failed||FPlatformTime::Seconds()>Deadline||
           Index>=star::terrain::MaxRangeFileBytes/BlockSize||
           (Size&&(Size>star::terrain::MaxRangeFileBytes||Index>(Size-1)/BlockSize)))return false;
        if(Blocks.Contains(Index))return true;
        const FString Path=Directory/TEXT("range-")+LexToString(Index)+TEXT(".bin");
        TArray<uint8> Bytes;
        const int64 CachedBytes=IFileManager::Get().FileSize(*Path);
        // Check on-disk size before allocating, then check again after reading.
        if(CachedBytes>0&&star::terrain::CompleteRangeBlock(Size,Index,static_cast<std::uint64_t>(CachedBytes))&&
           FFileHelper::LoadFileToArray(Bytes,*Path)&&star::terrain::CompleteRangeBlock(Size,Index,Bytes.Num()))
        {RememberBlock(Index,MoveTemp(Bytes));return true;}
        if(!Bytes.IsEmpty())StarDiagnostics::Event(TEXT("raster_cache_incomplete"),FString::Printf(TEXT("source=%s block=%llu bytes=%d; refetch"),*FPaths::GetCleanFilename(Directory),Index,Bytes.Num()));
        struct FResponse {std::atomic<bool> Done{false};int Code=0;TArray<uint8> Data;FString Range,Encoding;};
        auto Response=MakeShared<FResponse,ESPMode::ThreadSafe>();
        auto Request=FHttpModule::Get().CreateRequest();
        Request->SetURL(Url);Request->SetVerb(TEXT("GET"));Request->SetTimeout(20);
        Request->SetHeader(TEXT("Accept-Encoding"),TEXT("identity"));
        Request->SetHeader(TEXT("Range"),FString::Printf(TEXT("bytes=%llu-%llu"),Index*BlockSize,(Index+1)*BlockSize-1));
        Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
        Request->OnProcessRequestComplete().BindLambda([Response](FHttpRequestPtr,FHttpResponsePtr R,bool Ok){
            if(Ok&&R){
                Response->Code=R->GetResponseCode();
                const auto& Body=R->GetContent();
                // This avoids a second oversized copy. The HTTP backend's
                // initial receive-buffer limit still needs an engine-level test.
                if(Response->Code==206&&!Body.IsEmpty()&&static_cast<uint64>(Body.Num())<=BlockSize) {
                    Response->Range=R->GetHeader(TEXT("Content-Range"));
                    Response->Encoding=R->GetHeader(TEXT("Content-Encoding"));
                    if(star::terrain::IdentityRangeEncoding(TCHAR_TO_UTF8(*Response->Encoding)))Response->Data=Body;
                }
            }
            Response->Done.store(true);
        });
        if(!Request->ProcessRequest())return false;
        while(!Response->Done.load())
        {
            if(Cancel.load()||FPlatformTime::Seconds()>Deadline){Request->CancelRequest();
                StarDiagnostics::Event(Cancel.load()?TEXT("raster_cancelled"):TEXT("raster_timeout"),FString::Printf(TEXT("source=%s block=%llu"),*FPaths::GetCleanFilename(Directory),Index));return false;}
            FPlatformProcess::Sleep(0.01f);
        }
        if(Cancel.load()||FPlatformTime::Seconds()>Deadline)return false;
        if(Response->Code!=206||Response->Data.IsEmpty()||static_cast<uint64>(Response->Data.Num())>BlockSize){
            StarDiagnostics::Event(TEXT("raster_http_failed"),FString::Printf(TEXT("source=%s block=%llu http=%d bytes=%d"),*FPaths::GetCleanFilename(Directory),Index,Response->Code,Response->Data.Num()));return false;}
        std::uint64_t CheckedSize=0;
        if(!star::terrain::ValidateRangeResponse(TCHAR_TO_UTF8(*Response->Range),Index,Response->Data.Num(),Size,CheckedSize)){
            StarDiagnostics::Event(TEXT("raster_range_incomplete"),FString::Printf(TEXT("source=%s block=%llu bytes=%d total=%llu"),*FPaths::GetCleanFilename(Directory),Index,Response->Data.Num(),Size));return false;}
        Size=CheckedSize;
        IFileManager::Get().MakeDirectory(*Directory,true);
        FFileHelper::SaveArrayToFile(Response->Data,*Path);
        FFileHelper::SaveStringToFile(LexToString(Size),*(Directory/TEXT("size.txt")));
        FFileHelper::SaveStringToFile(Url,*(Directory/TEXT("source-url.txt")));
        RememberBlock(Index,MoveTemp(Response->Data));return true;
    }
};
tmsize_t Read(thandle_t Handle,void* Destination,tmsize_t Count)
{
    auto& R=*static_cast<FRangeReader*>(Handle);tmsize_t Done=0;
    if(Count<0||Count>512ll*1024*1024||(Count>0&&!Destination)){R.Failed=true;return 0;}
    while(Done<Count&&R.Position<R.Size)
    {
        uint64 Block=R.Position/FRangeReader::BlockSize,Offset=R.Position%FRangeReader::BlockSize;
        if(!R.Fetch(Block)){R.Failed=true;break;}
        const auto& Data=R.Blocks[Block];
        if(Offset>=static_cast<uint64>(Data.Num())){R.Failed=true;break;}
        const auto N=FMath::Min<int64>(Count-Done,Data.Num()-Offset);
        FMemory::Memcpy(static_cast<uint8*>(Destination)+Done,Data.GetData()+Offset,N);Done+=N;R.Position+=N;
    }
    return Done;
}
tmsize_t Write(thandle_t,void*,tmsize_t){return 0;}
toff_t Seek(thandle_t H,toff_t Offset,int Whence)
{
    auto& R=*static_cast<FRangeReader*>(H);
    std::uint64_t Next=0;
    if(R.Failed||!star::terrain::RangeSeekPosition(R.Size,R.Position,Offset,Whence,Next))
    {R.Failed=true;return static_cast<toff_t>(-1);}
    R.Position=Next;return R.Position;
}
int Close(thandle_t){return 0;}
toff_t Size(thandle_t H){return static_cast<FRangeReader*>(H)->Size;}
int Map(thandle_t,void**,toff_t*){return 0;}
void Unmap(thandle_t,void*,toff_t){}
}

bool FStarRemoteRaster::Load(const FString& Url,int32 MaximumDimension,const FString& Cache,const std::atomic<bool>& Cancel)
{
    if(MaximumDimension<=0){Error=TEXT("Invalid raster dimension limit");return false;}
    if(Cancel.load()){Error=TEXT("Raster read cancelled");return false;}
    FRangeReader R{Url,Cache/FMD5::HashAnsiString(*Url),Cancel};
    FString SizeText;
    const FString SizePath=R.Directory/TEXT("size.txt");
    const int64 SizeFileBytes=IFileManager::Get().FileSize(*SizePath);
    if(SizeFileBytes>0&&SizeFileBytes<=128&&FFileHelper::LoadFileToString(SizeText,*SizePath)) {
        std::uint64_t CachedSize=0;
        if(star::terrain::ParseRangeFileSize(TCHAR_TO_UTF8(*SizeText),CachedSize))R.Size=CachedSize;
    }
    if(!R.Fetch(0)||!R.Size){Error=TEXT("HTTP range unavailable");return false;}
    TIFF* T=TIFFClientOpen("STAR observed raster","rm",&R,Read,Write,Seek,Close,Size,Map,Unmap);
    if(!T){Error=TEXT("Invalid TIFF");return false;}
    int Best=-1;uint32 BestWidth=0,PendingNativeWidth=0,PendingNativeHeight=0;uint16 Directory=0;
    do {
        uint32 W=0,H=0;TIFFGetField(T,TIFFTAG_IMAGEWIDTH,&W);TIFFGetField(T,TIFFTAG_IMAGELENGTH,&H);
        if(Directory==0){
            if(!W||!H||W>2147483647u||H>2147483647u)
            {TIFFClose(T);Error=TEXT("Invalid native raster dimensions");return false;}
            PendingNativeWidth=W;PendingNativeHeight=H;
        }
        if(W&&H&&W<=static_cast<uint32>(MaximumDimension)&&H<=static_cast<uint32>(MaximumDimension)&&W>BestWidth){Best=Directory;BestWidth=W;}
        ++Directory;
    }while(Directory<32&&!Cancel.load()&&TIFFReadDirectory(T));
    if(Best<0||!TIFFSetDirectory(T,Best)){TIFFClose(T);Error=TEXT("No bounded overview");return false;}
    uint32 W=0,H=0,TW=0,TH=0;uint16 C=1,B=0,Format=1,Planar=1,Orientation=ORIENTATION_TOPLEFT;
    TIFFGetField(T,TIFFTAG_IMAGEWIDTH,&W);TIFFGetField(T,TIFFTAG_IMAGELENGTH,&H);
    TIFFGetFieldDefaulted(T,TIFFTAG_SAMPLESPERPIXEL,&C);TIFFGetFieldDefaulted(T,TIFFTAG_BITSPERSAMPLE,&B);
    TIFFGetFieldDefaulted(T,TIFFTAG_SAMPLEFORMAT,&Format);TIFFGetFieldDefaulted(T,TIFFTAG_PLANARCONFIG,&Planar);
    TIFFGetFieldDefaulted(T,TIFFTAG_ORIENTATION,&Orientation);
    if(!TIFFIsTiled(T)||Planar!=PLANARCONFIG_CONTIG||Orientation!=ORIENTATION_TOPLEFT||
       !C||C>4||!star::terrain::SupportedRasterSampleFormat(B,Format))
    {TIFFClose(T);Error=TEXT("Unsupported TIFF layout");return false;}
    TIFFGetField(T,TIFFTAG_TILEWIDTH,&TW);TIFFGetField(T,TIFFTAG_TILELENGTH,&TH);
    const int64 TileBytes=TIFFTileSize64(T),Stride=C*(B/8);
    std::uint64_t SampleBytes=0;
    if(TileBytes<=0||!star::terrain::BoundedRasterLayout(W,H,C,B,MaximumDimension,TW,TH,TileBytes,SampleBytes))
    {TIFFClose(T);Error=TEXT("Invalid or oversized raster layout");return false;}
    TArray<uint8> Tile,PendingSamples;
    Tile.SetNumUninitialized(static_cast<int32>(TileBytes));PendingSamples.SetNumZeroed(static_cast<int32>(SampleBytes));
    bool Ok=true;
    for(uint32 Y=0;Y<H&&Ok;Y+=TH)for(uint32 X=0;X<W&&Ok;X+=TW)
    {
        if(Cancel.load()){Ok=false;break;}
        const auto Decoded=TIFFReadEncodedTile(T,TIFFComputeTile(T,X,Y,0,0),Tile.GetData(),TileBytes);
        if(!star::terrain::CompleteDecodedTile(Decoded,static_cast<std::uint64_t>(TileBytes))){Ok=false;break;}
        for(uint32 Row=0;Row<FMath::Min(TH,H-Y);++Row)
            FMemory::Memcpy(PendingSamples.GetData()+(int64(Y+Row)*W+X)*Stride,Tile.GetData()+int64(Row)*TW*Stride,FMath::Min(TW,W-X)*Stride);
    }
    TIFFClose(T);
    if(!Ok||R.Failed||Cancel.load()||FPlatformTime::Seconds()>R.Deadline)
    {Error=TEXT("Incomplete raster; retained previous data or global fallback");return false;}
    // Publish metadata and bytes together; a reused raster remains valid after
    // any failed reload, including failure/cancellation on the final tile.
    Samples=MoveTemp(PendingSamples);SourceUrl=Url;
    NativeWidth=static_cast<int32>(PendingNativeWidth);NativeHeight=static_cast<int32>(PendingNativeHeight);
    Width=W;Height=H;Channels=C;Bits=B;Floating=Format==SAMPLEFORMAT_IEEEFP;
    Error.Empty();return true;
}
