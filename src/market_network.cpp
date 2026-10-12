// PS5 MarketBoard: unprivileged HTTPS fetching using PS5 sceHttp/sceSsl.
// Pattern based on blackbearreloaded/ps5-native-app-boilerplate's
// sandboxed update-check example, used with permission of GPL-3.0-or-later.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "market_network.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <climits>
#include <algorithm>
#include <ctime>
#include <pthread.h>

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
int sceHttpSendRequest(int, const void *, std::size_t);
int sceHttpGetStatusCode(int, int *);
int sceHttpGetAllResponseHeaders(int, char **, std::size_t *);
int sceHttpReadData(int, void *, std::size_t);
int sceKernelUsleep(std::uint32_t);
}

namespace market {
namespace {
constexpr std::uint32_t kTimeoutUs = 5000000;
constexpr int kProviderCount = 4;
constexpr std::size_t kResponseMax = 32768;
constexpr char kAgent[] = "PS5-MarketBoard/0.2";

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
FeedState state{};
pthread_t worker{};
bool started = false;
unsigned revision=0;
bool immediate=true;
long long cooldown[kProviderCount]{};
int failure_count[kProviderCount]{};

struct Response { int transport=0; int http_status=0; std::size_t used=0; int retry_after=0; };
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
long long now_seconds() noexcept { return static_cast<long long>(std::time(nullptr)); }
Response get_https(const char *url,char *buffer,std::size_t cap) noexcept {
    Response output{};
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

bool url_for(Provider p,const char *symbol,char *out,std::size_t n) noexcept {
    if(!symbol||!out||n<128)return false;
    const char *slash=std::strchr(symbol,'/');if(!slash)return false;
    char base[16]{};const std::size_t l=static_cast<std::size_t>(slash-symbol);
    if(l<2||l>=sizeof(base))return false;
    std::memcpy(base,symbol,l);
    const char *quote=slash+1;
    int used=0;
    switch(p){
        case Provider::Binance:
            used=std::snprintf(out,n,"https://api.binance.com/api/v3/ticker/24hr?symbol=%s%s",base,quote);break;
        case Provider::KuCoin:
            used=std::snprintf(out,n,"https://api.kucoin.com/api/v1/market/stats?symbol=%s-%s",base,quote);break;
        case Provider::Coinbase:
            used=std::snprintf(out,n,"https://api.exchange.coinbase.com/products/%s-%s/stats",base,quote);break;
        case Provider::CoinGecko: {
            if(std::strcmp(quote,"USD")!=0)return false;
            const char *id=coingecko_id(base);if(!id)return false;
            used=std::snprintf(out,n,"https://api.coingecko.com/api/v3/simple/price?ids=%s&vs_currencies=usd&include_24hr_change=true",id);break;
        }
        default:return false;
    }
    return used>0 && static_cast<std::size_t>(used)<n;
}
void note_failure(Provider p,int status,int retry_after,long long now) noexcept {
    int i=static_cast<int>(p);
    if(status==429){
        if(failure_count[i]<5)++failure_count[i];
        int backoff=30 << (failure_count[i]-1);
        if(backoff>600)backoff=600;
        if(retry_after>backoff)backoff=retry_after;
        cooldown[i]=now+backoff;
    } else if(status==403||status==451)cooldown[i]=now+120;
}
void note_success(Provider p) noexcept {
    const int i=static_cast<int>(p);
    failure_count[i]=0;cooldown[i]=0;
}
void update_entry(const char *sym,bool ok,const Quote &q,Provider p,int http,int transport) noexcept {
    (void)pthread_mutex_lock(&mutex);
    for(int i=0;i<state.prefs.count;++i){
        Entry &e=state.entries[i];
        if(std::strcmp(e.symbol,sym)!=0)continue;
        e.fresh=ok;
        e.error_code=http!=0?http:transport;
        if(ok){
            e.quote=q;e.provider=p;e.ready=true;e.updated_at=now_seconds();
            // Real observations only; no invented historical data.
            e.history[e.history_next]=q.price;
            e.history_at[e.history_next]=e.updated_at;
            e.history_next=(e.history_next+1)%kHistory;
            if(e.history_len<kHistory)++e.history_len;
            state.last_success_at=e.updated_at;
        }
        break;
    }
    state.last_http_status=http;
    state.last_transport_error=transport;
    (void)pthread_mutex_unlock(&mutex);
}
void fetch_pair(const char *pair) noexcept {
    const char *slash=std::strchr(pair,'/');if(!slash)return;
    char base[16]{};
    const std::size_t len=static_cast<std::size_t>(slash-pair);
    if(len==0||len>=sizeof(base))return;
    std::memcpy(base,pair,len);
    // Native quote matching: never substitute USDT for USD or vice versa.
    const bool usdt=std::strcmp(slash+1,"USDT")==0;
    const Provider order_usdt[]={Provider::Binance,Provider::KuCoin};
    const Provider order_usd[]={Provider::Coinbase,Provider::Binance,Provider::KuCoin,Provider::CoinGecko};
    const Provider *order=usdt?order_usdt:order_usd;
    const int n=usdt?2:4;
    Quote q{};int last_http=0,last_transport=0;
    for(int i=0;i<n;++i){
        const Provider p=order[i];
        if(cooldown[static_cast<int>(p)]>now_seconds())continue;
        char url[256]{};
        if(!url_for(p,pair,url,sizeof(url)))continue;
        char response[kResponseMax]{};
        const Response result=get_https(url,response,sizeof(response));
        last_http=result.http_status;last_transport=result.transport;
        if(result.transport==0&&result.http_status==200&&
           parse_provider_price(p,response,result.used,base,q)){
            note_success(p);
            update_entry(pair,true,q,p,result.http_status,0);
            return;
        }
        note_failure(p,result.http_status,result.retry_after,now_seconds());
    }
    update_entry(pair,false,q,Provider::None,last_http,last_transport);
}
void *run(void *) noexcept {
    for(;;){
        Preferences snapshot{};unsigned copied_revision=0;
        (void)pthread_mutex_lock(&mutex);
        snapshot=state.prefs;copied_revision=revision;
        state.updating=true;state.last_cycle_at=now_seconds();
        (void)pthread_mutex_unlock(&mutex);
        for(int i=0;i<snapshot.count;++i){
            fetch_pair(snapshot.pairs[i]);
            (void)pthread_mutex_lock(&mutex);
            const bool changed=revision!=copied_revision;
            (void)pthread_mutex_unlock(&mutex);
            if(changed)break;
        }
        (void)pthread_mutex_lock(&mutex);
        state.updating=false;
        ++state.completed_cycles;
        state.next_cycle_at=now_seconds()+kIntervals[state.prefs.interval_index];
        immediate=false;
        (void)pthread_mutex_unlock(&mutex);
        for(;;){
            (void)pthread_mutex_lock(&mutex);
            bool go=immediate||revision!=copied_revision||now_seconds()>=state.next_cycle_at;
            (void)pthread_mutex_unlock(&mutex);
            if(go)break;
            (void)sceKernelUsleep(250000);
        }
    }
    return nullptr;
}
}
void start_market_updates(const Preferences &p) noexcept {
    if(started)return;
    started=true;
    set_market_preferences(p);
    pthread_attr_t attr{};(void)pthread_attr_init(&attr);
    (void)pthread_attr_setstacksize(&attr,1024*1024);
    const int rc=pthread_create(&worker,&attr,run,nullptr);
    (void)pthread_attr_destroy(&attr);
    (void)pthread_mutex_lock(&mutex);
    state.worker_running=rc==0;
    if(rc!=0)state.last_transport_error=-100;
    (void)pthread_mutex_unlock(&mutex);
    if(rc==0)(void)pthread_detach(worker);
}
void set_market_preferences(const Preferences &p) noexcept {
    if(p.count<1||p.count>kMaxPairs||p.interval_index<0||p.interval_index>=kIntervalCount)return;
    (void)pthread_mutex_lock(&mutex);
    FeedState updated=state;
    updated.prefs=p;
    for(int i=0;i<p.count;++i){
        Entry found{};
        for(int j=0;j<state.prefs.count;++j)
            if(std::strcmp(state.entries[j].symbol,p.pairs[i])==0){found=state.entries[j];break;}
        updated.entries[i]=found;
        std::strcpy(updated.entries[i].symbol,p.pairs[i]);
    }
    for(int i=p.count;i<kMaxPairs;++i)updated.entries[i]=Entry{};
    state=updated;
    ++revision;immediate=true;
    (void)pthread_mutex_unlock(&mutex);
}
void force_market_refresh() noexcept {
    (void)pthread_mutex_lock(&mutex);immediate=true;(void)pthread_mutex_unlock(&mutex);
}
FeedState read_market_state() noexcept {
    (void)pthread_mutex_lock(&mutex);
    FeedState copy=state;
    (void)pthread_mutex_unlock(&mutex);
    return copy;
}
}
