// PS5 MarketBoard: bounded, certificate-verified HTTPS transport.
// Based on the GPL-3.0-or-later sandboxed update-check pattern.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "market_http.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>

extern "C" {
int sceNetPoolCreate(const char *, int, int);
int sceNetPoolDestroy(int);
int sceSslInit(std::size_t);
int sceSslTerm(int);
int sceHttpInit(int, int, std::size_t);
int sceHttpTerm(int);
int sceHttpCreateTemplate(int, const char *, int, int);
int sceHttpDeleteTemplate(int);
int sceHttpCreateConnectionWithURL(int, const char *, int);
int sceHttpDeleteConnection(int);
int sceHttpCreateRequestWithURL(int, int, const char *, std::uint64_t);
int sceHttpDeleteRequest(int);
int sceHttpSetAutoRedirect(int, int);
int sceHttpSetResolveTimeOut(int, std::uint32_t);
int sceHttpSetConnectTimeOut(int, std::uint32_t);
int sceHttpSetSendTimeOut(int, std::uint32_t);
int sceHttpSetRecvTimeOut(int, std::uint32_t);
int sceHttpsEnableOption(int, std::uint32_t);
int sceHttpAddRequestHeader(int, const char *, const char *, int);
int sceHttpSendRequest(int, const void *, std::size_t);
int sceHttpGetStatusCode(int, int *);
int sceHttpGetAllResponseHeaders(int, char **, std::size_t *);
int sceHttpReadData(int, void *, std::size_t);
}

namespace market {
namespace {
constexpr std::uint32_t kTimeoutUs=5000000;
constexpr char kAgent[]="PS5-MarketBoard/0.3";
constexpr char kCoinGeckoRoot[]="https://api.coingecko.com/";
int retry_after_seconds(const char *headers,std::size_t length) noexcept {
    if(!headers || length>16384)return 0;
    // Only accept numeric Retry-After (the widely used form for exchange APIs).
    const char needle[]="retry-after:";
    for(std::size_t i=0;i+sizeof(needle)-1<length;++i){
        if(i>0 && headers[i-1]!='\n')continue;
        std::size_t j=0;
        for(;j<sizeof(needle)-1;++j){
            const char c=headers[i+j];
            const char lower=c>='A'&&c<='Z'?static_cast<char>(c+32):c;
            if(lower!=needle[j])break;
        }
        if(j!=sizeof(needle)-1)continue;
        i+=j;
        while(i<length && (headers[i]==' '||headers[i]=='\t'))++i;
        int num=0;bool found=false;
        while(i<length&&headers[i]>='0'&&headers[i]<='9'){
            found=true;
            if(num>3600)return 3600;
            num=num*10+(headers[i++]-'0');
        }
        return found?std::min(num,3600):0;
    }
    return 0;
}
}
HttpResponse get_https(const char *url,char *buffer,std::size_t cap,const char *demo_key) noexcept {
    HttpResponse output{};
    if(!url||!buffer||cap<2)return {-1,0,0};
    int pool=-1,ssl=-1,http=-1,tpl=-1,connection=-1,request=-1;
    int rc=pool=sceNetPoolCreate("marketboard",1024*1024,0);
    if(rc>=0)rc=ssl=sceSslInit(304*1024);
    if(rc>=0)rc=http=sceHttpInit(pool,ssl,4*1024*1024);
    if(rc>=0)rc=tpl=sceHttpCreateTemplate(http,kAgent,2,0);
    if(rc>=0)rc=sceHttpSetAutoRedirect(tpl,0);
    if(rc>=0)rc=sceHttpSetResolveTimeOut(tpl,kTimeoutUs);
    if(rc>=0)rc=sceHttpSetConnectTimeOut(tpl,kTimeoutUs);
    if(rc>=0)rc=sceHttpSetSendTimeOut(tpl,kTimeoutUs);
    if(rc>=0)rc=sceHttpSetRecvTimeOut(tpl,kTimeoutUs);
    // Verify certificate, hostname, lifetime, CA and SNI. Never ignore TLS errors.
    if(rc>=0)rc=sceHttpsEnableOption(tpl,0x01|0x04|0x08|0x10|0x20|0x80);
    if(rc>=0)rc=connection=sceHttpCreateConnectionWithURL(tpl,url,0);
    if(rc>=0)rc=request=sceHttpCreateRequestWithURL(connection,0,url,0);
    if(rc>=0&&demo_key&&demo_key[0]&&
       std::strncmp(url,kCoinGeckoRoot,sizeof(kCoinGeckoRoot)-1)==0)
        rc=sceHttpAddRequestHeader(request,"x-cg-demo-api-key",demo_key,0);
    if(rc>=0)rc=sceHttpSendRequest(request,nullptr,0);
    if(rc>=0)rc=sceHttpGetStatusCode(request,&output.http_status);
    if(rc>=0 && output.http_status==429){
        char *headers=nullptr;
        std::size_t length=0;
        if(sceHttpGetAllResponseHeaders(request,&headers,&length)>=0)
            output.retry_after=retry_after_seconds(headers,length);
    }
    while(rc>=0){
        char excess;
        void *dst=output.used+1<cap?buffer+output.used:&excess;
        const std::size_t space=output.used+1<cap?cap-output.used-1:1;
        int n=sceHttpReadData(request,dst,space);
        if(n<0){rc=n;break;}
        if(n==0)break;
        if(output.used+1>=cap){rc=-2;break;}
        output.used+=static_cast<std::size_t>(n);
    }
    if(request>=0)(void)sceHttpDeleteRequest(request);
    if(connection>=0)(void)sceHttpDeleteConnection(connection);
    if(tpl>=0)(void)sceHttpDeleteTemplate(tpl);
    if(http>=0)(void)sceHttpTerm(http);
    if(ssl>=0)(void)sceSslTerm(ssl);
    if(pool>=0)(void)sceNetPoolDestroy(pool);
    buffer[output.used]=0;
    output.transport=rc<0?rc:0;
    return output;
}

}
