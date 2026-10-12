// SPDX-License-Identifier: GPL-3.0-or-later
#include "market_icon_core.hpp"
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>

namespace market {
namespace {
char upper(char c) noexcept {return c>='a'&&c<='z'?static_cast<char>(c-32):c;}
bool symbol_equal(const std::string &symbol,const char *base) noexcept {
    if(symbol.size()!=std::strlen(base))return false;
    for(std::size_t i=0;i<symbol.size();++i)if(upper(symbol[i])!=base[i])return false;
    return true;
}
bool image_url(const std::string &url) noexcept {
    constexpr const char *roots[]={"https://coin-images.coingecko.com/coins/images/",
                                 "https://assets.coingecko.com/coins/images/"};
    bool allowed=false;
    for(const char *root:roots)if(url.compare(0,std::strlen(root),root)==0)allowed=true;
    if(!allowed||url.size()>1023)return false;
    for(unsigned char c:url)if(c<=32||c>=127||c=='\\'||c=='#')return false;
    return url.find("..") == std::string::npos;
}

// Small bounded JSON cursor: respects object boundaries, escapes and types.
// It deliberately does not search for fields inside arbitrary JSON strings.
class Json {
 public:
    Json(const char *data,std::size_t size):p_(data),end_(data+size){}
    void space(){while(p_<end_&&(*p_==' '||*p_=='\r'||*p_=='\n'||*p_=='\t'))++p_;}
    bool take(char c){space();if(p_==end_||*p_!=c)return false;++p_;return true;}
    bool done(){space();return p_==end_;}
    bool string(std::string &out){
        if(!take('"'))return false;
        out.clear();
        while(p_<end_){
            unsigned char c=static_cast<unsigned char>(*p_++);
            if(c=='"')return true;
            if(c<32)return false;
            if(c=='\\'){
                if(p_==end_)return false;
                c=static_cast<unsigned char>(*p_++);
                switch(c){
                    case '"':case '\\':case '/':break;
                    case 'b':c='\b';break;case 'f':c='\f';break;
                    case 'n':c='\n';break;case 'r':c='\r';break;case 't':c='\t';break;
                    case 'u':{
                        unsigned value=0;
                        for(int i=0;i<4;++i){
                            if(p_==end_)return false;
                            const char h=*p_++;
                            const int digit=h>='0'&&h<='9'?h-'0':
                                h>='a'&&h<='f'?h-'a'+10:h>='A'&&h<='F'?h-'A'+10:-1;
                            if(digit<0)return false;
                            value=value*16+static_cast<unsigned>(digit);
                        }
                        // Identity/URL fields are ASCII. Other text is discarded.
                        c=value<128?static_cast<unsigned char>(value):0xff;
                        break;
                    }
                    default:return false;
                }
            }
            out.push_back(static_cast<char>(c));
        }
        return false;
    }
    bool skip(int depth=0){
        if(depth>16)return false;
        space();if(p_==end_)return false;
        if(*p_=='"'){std::string ignored;return string(ignored);}
        if(*p_=='{'||*p_=='['){
            const bool object=*p_++=='{';const char close=object?'}':']';
            if(take(close))return true;
            for(;;){
                if(object){std::string key;if(!string(key)||!take(':'))return false;}
                if(!skip(depth+1))return false;
                if(take(close))return true;
                if(!take(','))return false;
            }
        }
        for(const char *literal:{"true","false","null"}){
            const std::size_t n=std::strlen(literal);
            if(static_cast<std::size_t>(end_-p_)>=n&&std::memcmp(p_,literal,n)==0){p_+=n;return true;}
        }
        if(*p_=='-')++p_;
        if(p_==end_)return false;
        if(*p_=='0')++p_;
        else{
            if(*p_<'1'||*p_>'9')return false;
            do{++p_;}while(p_<end_&&*p_>='0'&&*p_<='9');
        }
        if(p_<end_&&*p_=='.'){
            ++p_;const char *start=p_;
            while(p_<end_&&*p_>='0'&&*p_<='9')++p_;
            if(start==p_)return false;
        }
        if(p_<end_&&(*p_=='e'||*p_=='E')){
            ++p_;if(p_<end_&&(*p_=='+'||*p_=='-'))++p_;
            const char *start=p_;while(p_<end_&&*p_>='0'&&*p_<='9')++p_;
            if(start==p_)return false;
        }
        return true;
    }
 private:
    const char *p_,*end_;
};

struct Candidate {std::string id,symbol,large,thumb;};
bool candidate(Json &json,Candidate &c){
    if(!json.take('{'))return false;
    if(json.take('}'))return true;
    unsigned seen=0;
    for(;;){
        std::string key;if(!json.string(key)||!json.take(':'))return false;
        unsigned bit=0;std::string *target=nullptr;
        if(key=="id"){bit=1;target=&c.id;}
        else if(key=="symbol"){bit=2;target=&c.symbol;}
        else if(key=="large"){bit=4;target=&c.large;}
        else if(key=="thumb"){bit=8;target=&c.thumb;}
        if(target){if((seen&bit)||!json.string(*target))return false;seen|=bit;}
        else if(!json.skip())return false;
        if(json.take('}'))return true;
        if(!json.take(','))return false;
    }
}
}

bool valid_icon_base(const char *base) noexcept {
    if(!base)return false;
    std::size_t n=0;
    for(;base[n];++n){
        if(n>=10||!((base[n]>='A'&&base[n]<='Z')||(base[n]>='0'&&base[n]<='9')))return false;
    }
    return n>=2;
}
bool icon_base(const char *pair,char out[kIconBaseSize]) noexcept {
    char clean[kSymbolSize]{};
    if(!out||!normalize_pair(pair,clean))return false;
    const auto length=static_cast<std::size_t>(std::strchr(clean,'/')-clean);
    std::memcpy(out,clean,length);out[length]=0;return true;
}
bool bundled_icon(const char *base) noexcept {
    constexpr const char *symbols[]={"BTC","ETH","SOL","XRP","DOGE","USDT"};
    if(!base)return false;
    for(const char *symbol:symbols)if(std::strcmp(base,symbol)==0)return true;
    return false;
}
bool icon_path(const char *root,const char *base,const char *extension,char *out,std::size_t cap) noexcept {
    if(!root||!out||!extension||!valid_icon_base(base)||
       (std::strcmp(extension,"png")!=0&&std::strcmp(extension,"img")!=0))return false;
    const int n=std::snprintf(out,cap,"%s/%s.%s",root,base,extension);
    return n>0&&static_cast<std::size_t>(n)<cap;
}
std::uint32_t icon_color(const char *base) noexcept {
    constexpr std::uint32_t palette[]={0x447f91,0x7463a7,0x347e68,0x9a7248,0x936079,0x566caa};
    std::uint32_t hash=2166136261u;
    if(base)for(const char *c=base;*c;++c){hash^=static_cast<unsigned char>(*c);hash*=16777619u;}
    return palette[hash%6];
}
bool parse_icon_search(const char *data,std::size_t size,const char *base,char *url,std::size_t cap) noexcept {
    if(!data||!size||size>kIconResponseMax||!valid_icon_base(base)||!url||cap<2)return false;
    Json json(data,size);
    if(!json.take('{'))return false;
    bool saw_coins=false;int matches=0;std::string chosen;
    const char *known=coingecko_id(base);
    if(!json.take('}'))for(;;){
        std::string key;if(!json.string(key)||!json.take(':'))return false;
        if(key=="coins"){
            if(saw_coins||!json.take('['))return false;
            saw_coins=true;
            if(!json.take(']'))for(;;){
                Candidate c;if(!candidate(json,c))return false;
                if(!c.id.empty()&&symbol_equal(c.symbol,base)&&(!known||c.id==known)){
                    ++matches;
                    chosen=image_url(c.large)?c.large:c.thumb;
                }
                if(json.take(']'))break;
                if(!json.take(','))return false;
            }
        }else if(!json.skip())return false;
        if(json.take('}'))break;
        if(!json.take(','))return false;
    }
    if(!json.done()||!saw_coins||matches!=1||!image_url(chosen)||chosen.size()>=cap)return false;
    std::memcpy(url,chosen.c_str(),chosen.size()+1);return true;
}
}
