#pragma once
#include "CoreMinimal.h"
struct FHttpResponse {
    int Code=206;
    TArray<uint8> Body;
    std::map<std::string,std::string> Headers;
    int GetResponseCode()const{return Code;}
    const TArray<uint8>& GetContent()const{return Body;}
    FString GetHeader(const char* name)const{auto it=Headers.find(name);return it==Headers.end()?FString():FString(it->second);}
};
struct FHttpRequest;
using FHttpRequestPtr=std::shared_ptr<FHttpRequest>;
using FHttpResponsePtr=std::shared_ptr<FHttpResponse>;
namespace RasterTest {
inline std::function<FHttpResponsePtr(const FHttpRequest&)> Server;
inline bool RequestStarts=true,RequestCompletes=true;
inline int Requests=0,CancelledRequests=0;
}
enum class EHttpRequestDelegateThreadPolicy {CompleteOnHttpThread};
struct FHttpRequest {
    FString Url;
    std::map<std::string,std::string> Headers;
    struct Completion {
        std::function<void(FHttpRequestPtr,FHttpResponsePtr,bool)> Function;
        template<class F>void BindLambda(F&& fn){Function=std::forward<F>(fn);}
    } Complete;
    void SetURL(const FString& url){Url=url;}
    void SetVerb(const char*){}
    void SetTimeout(int){}
    void SetHeader(const char* name,const FString& value){Headers[name]=value.Value;}
    void SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy){}
    Completion& OnProcessRequestComplete(){return Complete;}
    bool ProcessRequest(){
        ++RasterTest::Requests;
        if(!RasterTest::RequestStarts)return false;
        if(RasterTest::RequestCompletes){auto response=RasterTest::Server?RasterTest::Server(*this):nullptr;Complete.Function(nullptr,response,bool(response));}
        return true;
    }
    void CancelRequest(){++RasterTest::CancelledRequests;}
};
struct FHttpModule {
    static FHttpModule& Get(){static FHttpModule module;return module;}
    FHttpRequestPtr CreateRequest(){return std::make_shared<FHttpRequest>();}
};
