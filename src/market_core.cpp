// SPDX-License-Identifier: GPL-3.0-or-later
#include "market_core.hpp"
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace market {
namespace {
constexpr bool ascii_alnum(char c) noexcept { return (c>='A'&&c<='Z')||(c>='0'&&c<='9'); }
constexpr char upper(char c) noexcept { return c>='a'&&c<='z' ? static_cast<char>(c-32) : c; }
void trim(char *s) noexcept {
    const std::size_t n=std::strlen(s);
    std::size_t j=0;
    for(std::size_t i=0;i<n;++i) if(s[i]!=' '&&s[i]!='\t'&&s[i]!='\r'&&s[i]!='\n')s[j++]=s[i];
    s[j]=0;
}
// Search for complete string keys. Bounded input and strictly typed following values.
// Known exchange responses use unique keys; the object identity is verified for CoinGecko.
bool field(const char *json, std::size_t size, const char *key,
           char *out, std::size_t capacity) noexcept {
    if(!json||size==0||size>32768||!key||!out||capacity<2) return false;
    const std::size_t klen=std::strlen(key);
    const char *end=json+size;
    for(const char *p=json;p+klen+2<end;++p){
        if(*p!='"'||std::memcmp(p+1,key,klen)!=0||p[klen+1]!='"')continue;
        const char *q=p+klen+2;
        while(q<end&&(*q==' '||*q=='\n'||*q=='\r'||*q=='\t'))++q;
        if(q==end||*q++!=':')continue;
        while(q<end&&(*q==' '||*q=='\n'||*q=='\r'||*q=='\t'))++q;
        if(q==end) return false;
        const bool quoted=*q=='"';
        if(quoted)++q;
        const char *begin=q;
        if(quoted){ while(q<end&&*q!='"'&&*q!='\\')++q; if(q==end||*q!='"')return false; }
        else { while(q<end&&(*q=='-'||*q=='+'||*q=='.'||(*q>='0'&&*q<='9')||*q=='e'||*q=='E'))++q; }
        if(q==begin || static_cast<std::size_t>(q-begin)>=capacity)return false;
        std::memcpy(out,begin,static_cast<std::size_t>(q-begin));out[q-begin]=0;
        return true;
    }
    return false;
}
bool number_field(const char *j,std::size_t n,const char *key,double &value) noexcept {
    char buf[64]{};
    if(!field(j,n,key,buf,sizeof(buf)))return false;
    errno=0; char *end=nullptr;
    const double x=std::strtod(buf,&end);
    if(errno==ERANGE||end==buf||*end||!std::isfinite(x))return false;
    value=x;return true;
}
} // namespace
bool normalize_pair(const char *input,char output[kSymbolSize]) noexcept {
    if(!input||!output)return false;
    char tmp[kSymbolSize]{};
    std::size_t n=0;
    for(const char *p=input;*p;++p){
        if(n>=kSymbolSize-1)return false;
        tmp[n++]=upper(*p);
    }
    trim(tmp);
    const char *slash=std::strchr(tmp,'/');
    if(!slash||std::strchr(slash+1,'/'))return false;
    const std::size_t base=static_cast<std::size_t>(slash-tmp);
    if(base<2||base>10)return false;
    for(std::size_t i=0;i<base;++i)if(!ascii_alnum(tmp[i]))return false;
    if(std::strcmp(slash+1,"USD")!=0&&std::strcmp(slash+1,"USDT")!=0)return false;
    std::memcpy(output,tmp,n+1);
    return true;
}
bool add_pair(Preferences &p,const char *symbol) noexcept {
    char normalized[kSymbolSize]{};
    if(p.count<0||p.count>=kMaxPairs||!normalize_pair(symbol,normalized))return false;
    for(int i=0;i<p.count;++i)if(std::strcmp(p.pairs[i],normalized)==0)return false;
    std::strcpy(p.pairs[p.count++],normalized);
    return true;
}
bool remove_pair(Preferences &p,int idx) noexcept {
    if(idx<0||idx>=p.count||p.count<=1)return false;
    for(int i=idx;i<p.count-1;++i)std::strcpy(p.pairs[i],p.pairs[i+1]);
    p.pairs[--p.count][0]=0;
    return true;
}
bool serialize_prefs(const Preferences &p,char *b,std::size_t cap) noexcept {
    if(!b||cap<64||p.count<1||p.count>kMaxPairs||p.interval_index<0||p.interval_index>=kIntervalCount)return false;
    int n=std::snprintf(b,cap,"MARKETBOARD 1\nINTERVAL %d\n",kIntervals[p.interval_index]);
    if(n<0||static_cast<std::size_t>(n)>=cap)return false;
    std::size_t len=static_cast<std::size_t>(n);
    for(int i=0;i<p.count;++i){
        char clean[kSymbolSize];if(!normalize_pair(p.pairs[i],clean))return false;
        n=std::snprintf(b+len,cap-len,"PAIR %s\n",clean);
        if(n<0||static_cast<std::size_t>(n)>=cap-len)return false;
        len+=static_cast<std::size_t>(n);
    }
    return true;
}
bool parse_prefs(const char *text,std::size_t size,Preferences &out) noexcept {
    if(!text||size==0||size>2048)return false;
    char buf[2049]{};std::memcpy(buf,text,size);
    if(std::strncmp(buf,"MARKETBOARD 1\n",14)!=0)return false;
    Preferences next{};next.count=0;
    bool saw_interval=false;
    char *line=buf+14;
    while(line&&*line){
        char *end=std::strchr(line,'\n');if(end)*end=0;
        if(std::strncmp(line,"INTERVAL ",9)==0){
            if(saw_interval)return false;
            saw_interval=true;
            const char *number=line+9;
            if(!*number)return false;
            for(const char *c=number;*c;++c)if(*c<'0'||*c>'9')return false;
            const int seconds=std::atoi(number);
            int found=-1;
            for(int i=0;i<kIntervalCount;++i)if(kIntervals[i]==seconds)found=i;
            if(found<0)return false;
            next.interval_index=found;
        } else if(std::strncmp(line,"PAIR ",5)==0){
            if(!add_pair(next,line+5))return false;
        }else if(*line)return false;
        line=end?end+1:nullptr;
    }
    if(!saw_interval||next.count<1)return false;
    out=next;return true;
}
const char *provider_name(Provider p) noexcept {
    switch(p){case Provider::Binance:return "BINANCE";case Provider::KuCoin:return "KUCOIN";
      case Provider::Coinbase:return "COINBASE";case Provider::CoinGecko:return "COINGECKO";
      default:return "--";}
}
const char *coingecko_id(const char *base) noexcept {
    struct Mapping {const char *symbol;const char *id;};
    static constexpr Mapping items[]={
        {"BTC","bitcoin"},{"ETH","ethereum"},{"SOL","solana"},{"XRP","ripple"},
        {"DOGE","dogecoin"},{"ADA","cardano"},{"AVAX","avalanche-2"},
        {"LINK","chainlink"},{"DOT","polkadot"},{"LTC","litecoin"},
        {"UNI","uniswap"},{"BNB","binancecoin"},{"TRX","tron"},{"SHIB","shiba-inu"}
    };
    if(!base)return nullptr;
    for(const auto &it:items)if(std::strcmp(base,it.symbol)==0)return it.id;
    return nullptr;
}
bool parse_provider_price(Provider source,const char *j,std::size_t size,
                          const char *base,Quote &out) noexcept {
    if(!j||!size||size>32768)return false;
    Quote next{};
    switch(source){
        case Provider::Binance:
            if(!number_field(j,size,"lastPrice",next.price))return false;
            next.has_change=number_field(j,size,"priceChangePercent",next.change_24h);
            break;
        case Provider::KuCoin: {
            // KuCoin /market/stats replies with code 200000 and nested data.
            char code[30]{};
            if(!field(j,size,"code",code,sizeof(code))||std::strcmp(code,"200000")!=0)return false;
            if(!number_field(j,size,"last",next.price))return false;
            double r=0;
            if(number_field(j,size,"changeRate",r)){next.has_change=true;next.change_24h=r*100.0;}
            break;
        }
        case Provider::Coinbase:{
            if(!number_field(j,size,"last",next.price))return false;
            double open=0;
            if(number_field(j,size,"open",open)&&open>0){next.has_change=true;next.change_24h=100.0*(next.price/open-1.0);}
            break;
        }
        case Provider::CoinGecko: {
            if(!base||!coingecko_id(base))return false;
            // Endpoint requests exactly one CoinGecko id, keeping the response scoped.
            if(!number_field(j,size,"usd",next.price))return false;
            next.has_change=number_field(j,size,"usd_24h_change",next.change_24h);
            break;
        }
        default:return false;
    }
    if(!std::isfinite(next.price)||next.price<=0||next.price>1e15||
       (next.has_change&&(!std::isfinite(next.change_24h)||std::fabs(next.change_24h)>100000)))return false;
    out=next;return true;
}
}
