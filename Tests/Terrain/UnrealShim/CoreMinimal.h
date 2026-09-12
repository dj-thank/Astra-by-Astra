#pragma once
// Offline adapter test double, NOT Unreal Engine. It supplies only the APIs used
// by StarRemoteRaster.cpp. Decoding uses the real system libtiff library.
#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using uint8=std::uint8_t;
using uint16=std::uint16_t;
using uint32=std::uint32_t;
using uint64=unsigned long long;
using int32=std::int32_t;
using int64=long long;
#define TEXT(x) x
#define TCHAR_TO_UTF8(x) x
#define THIRD_PARTY_INCLUDES_START
#define THIRD_PARTY_INCLUDES_END

namespace RasterTest {
inline std::size_t ArrayCopies=0;
inline std::vector<std::string> ArrayReads,StringReads;
inline bool WriteFiles=true;
inline double Now=0;
}
struct FString {
    std::string Value;
    FString()=default;
    FString(const char* value):Value(value){}
    FString(std::string value):Value(std::move(value)){}
    const char* operator*() const{return Value.c_str();}
    bool IsEmpty() const{return Value.empty();}
    void Empty(){Value.clear();}
    static FString Printf(const char* format,...) {
        va_list args;va_start(args,format);va_list sizeArgs;va_copy(sizeArgs,args);
        const int size=std::vsnprintf(nullptr,0,format,sizeArgs);va_end(sizeArgs);
        std::vector<char> buffer(static_cast<std::size_t>(std::max(0,size))+1);
        std::vsnprintf(buffer.data(),buffer.size(),format,args);va_end(args);
        return FString(buffer.data());
    }
    friend FString operator+(const FString& a,const FString& b){return a.Value+b.Value;}
    friend FString operator/(const FString& a,const FString& b){return (std::filesystem::path(a.Value)/b.Value).string();}
    friend bool operator==(const FString& a,const FString& b){return a.Value==b.Value;}
};
template<class T>FString LexToString(T value){return std::to_string(value);}
template<class T>auto&& MoveTemp(T& value){return std::move(value);}
template<class T>struct TArray {
    std::vector<T> Values;
    TArray()=default;
    TArray(const TArray& other):Values(other.Values){RasterTest::ArrayCopies+=Values.size()*sizeof(T);}
    TArray(TArray&&)=default;
    TArray& operator=(const TArray& other){Values=other.Values;RasterTest::ArrayCopies+=Values.size()*sizeof(T);return *this;}
    TArray& operator=(TArray&&)=default;
    int32 Num()const{return static_cast<int32>(Values.size());}
    bool IsEmpty()const{return Values.empty();}
    void SetNumUninitialized(int32 size){Values.resize(static_cast<std::size_t>(size));}
    void SetNumZeroed(int32 size){Values.assign(static_cast<std::size_t>(size),T{});}
    void Empty(){Values.clear();}
    T* GetData(){return Values.data();}
    const T* GetData()const{return Values.data();}
    T& operator[](std::size_t i){return Values.at(i);}
    const T& operator[](std::size_t i)const{return Values.at(i);}
    void Add(const T& value){Values.push_back(value);}
    void RemoveAt(int32 index){Values.erase(Values.begin()+index);}
};
template<class K,class V>struct TMap {
    std::map<K,V> Values;
    bool Contains(K key)const{return Values.find(key)!=Values.end();}
    V& operator[](K key){return Values.at(key);}
    void Add(K key,V&& value){Values.insert_or_assign(key,std::move(value));}
    int32 Num()const{return static_cast<int32>(Values.size());}
    void Remove(K key){Values.erase(key);}
    void Empty(){Values.clear();}
};
namespace ESPMode {enum Type{ThreadSafe};}
template<class T,ESPMode::Type Mode,class...A>std::shared_ptr<T> MakeShared(A&&...args){return std::make_shared<T>(std::forward<A>(args)...);}
struct FMath {template<class T>static T Min(T a,T b){return std::min(a,b);}};
struct FMemory {static void Memcpy(void* dst,const void* src,std::size_t bytes){std::memcpy(dst,src,bytes);}};
struct FPlatformTime {static double Seconds(){return RasterTest::Now;}};
struct FPlatformProcess {static void Sleep(float seconds){RasterTest::Now+=seconds;}};
struct FPaths {static FString GetCleanFilename(const FString& path){return std::filesystem::path(path.Value).filename().string();}};
struct FMD5 {
    // A deterministic test-only cache key, NOT an MD5 implementation.
    static FString HashAnsiString(const char* text){uint64 hash=14695981039346656037ull;for(;*text;++text){hash^=static_cast<unsigned char>(*text);hash*=1099511628211ull;}return std::to_string(hash);}
};
struct IFileManager {
    static IFileManager& Get(){static IFileManager manager;return manager;}
    bool FileExists(const char* path){return std::filesystem::is_regular_file(path);}
    int64 FileSize(const char* path){std::error_code error;const auto size=std::filesystem::file_size(path,error);return error?-1:static_cast<int64>(size);}
    bool MakeDirectory(const char* path,bool){std::error_code error;std::filesystem::create_directories(path,error);return !error;}
};
struct FFileHelper {
    static bool LoadFileToArray(TArray<uint8>& out,const char* path){
        RasterTest::ArrayReads.emplace_back(path);std::ifstream file(path,std::ios::binary);if(!file)return false;
        out.Values.assign(std::istreambuf_iterator<char>(file),{});return true;
    }
    static bool LoadFileToString(FString& out,const char* path){
        RasterTest::StringReads.emplace_back(path);std::ifstream file(path,std::ios::binary);if(!file)return false;
        out.Value.assign(std::istreambuf_iterator<char>(file),{});return true;
    }
    static bool SaveArrayToFile(const TArray<uint8>& data,const char* path){
        if(!RasterTest::WriteFiles)return false;
        std::ofstream file(path,std::ios::binary);
        file.write(reinterpret_cast<const char*>(data.GetData()),data.Num());return bool(file);
    }
    static bool SaveStringToFile(const FString& data,const char* path){
        if(!RasterTest::WriteFiles)return false;
        std::ofstream file(path,std::ios::binary);file<<data.Value;return bool(file);
    }
};
namespace StarDiagnostics {inline void Event(const char*,const FString&){} }
