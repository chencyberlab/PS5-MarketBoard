// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/market_icon_core.hpp"
#include "../src/market_icon_image.hpp"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int main(int argc,char **argv){
    using namespace market;
    char base[kIconBaseSize]{},path[192]{},url[1100]{};
    assert(icon_base("doge/usdt",base)&&std::strcmp(base,"DOGE")==0);
    assert(!icon_base("../BTC/USDT",base));
    assert(!valid_icon_base("BTC/..")&&!valid_icon_base("ABCDEFGHIJK"));
    assert(bundled_icon("BTC")&&bundled_icon("USDT")&&!bundled_icon("NEAR"));
    assert(icon_path(kIconCacheRoot,"DOGE","img",path,sizeof(path)));
    assert(std::strcmp(path,"/download0/marketboard/icons/DOGE.img")==0);
    assert(!icon_path(kIconCacheRoot,"../DOGE","img",path,sizeof(path)));
    assert(!icon_path(kIconCacheRoot,"BTC","img",path,4));
    assert(icon_color("NEAR")==icon_color("NEAR"));

    auto parse=[&](const char *data,const char *symbol){
        return parse_icon_search(data,std::strlen(data),symbol,url,sizeof(url));
    };
    const char *unique=R"({"coins":[{"id":"near","symbol":"near","large":"https://coin-images.coingecko.com/coins/images/10365/large/near.png?1"}],"exchanges":[{"symbol":"NEAR","large":"https://example.com/wrong.png"}]})";
    assert(parse(unique,"NEAR"));
    assert(std::strcmp(url,"https://coin-images.coingecko.com/coins/images/10365/large/near.png?1")==0);
    const std::string saved=url;
    assert(!parse(unique,"NEA")&&saved==url);
    assert(!parse(R"({"coins":[{"id":"a","symbol":"ABC","large":"https://assets.coingecko.com/coins/images/1/large/a.png"},{"id":"b","symbol":"ABC","large":"https://assets.coingecko.com/coins/images/2/large/b.png"}]})","ABC"));
    // A mapped ID resolves a duplicate symbol, independent of result ordering.
    assert(parse(R"({"coins":[{"id":"fake-bitcoin","symbol":"BTC","large":"https://assets.coingecko.com/coins/images/2/large/fake.png"},{"id":"bitcoin","symbol":"btc","large":"https://assets.coingecko.com/coins/images/1/large/bitcoin.png"}]})","BTC"));
    assert(!parse(R"({"coins":[{"id":"fake-bitcoin","symbol":"BTC","large":"https://assets.coingecko.com/coins/images/2/large/fake.png"}]})","BTC"));
    assert(!parse(R"({"coins":[],"nfts":[{"symbol":"ABC","large":"https://assets.coingecko.com/coins/images/1/large/a.png"}]})","ABC"));
    assert(!parse(R"({"coins":[{"symbol":"ABC","large":"https://assets.coingecko.com/coins/images/1/large/a.png"}]})","ABC"));
    assert(!parse(R"({"coins":[{"id":"abc","symbol":"ABC","large":"http://assets.coingecko.com/coins/images/1/a.png"}]})","ABC"));
    assert(!parse(R"({"coins":[{"id":"abc","symbol":"ABC","large":"https://assets.coingecko.com.evil.test/coins/images/1/a.png"}]})","ABC"));
    assert(!parse(R"({"coins":[{"id":"abc","symbol":"ABC","large":"https://assets.coingecko.com/coins/images/../a.png"}]})","ABC"));
    assert(!parse(R"({"coins":[{"id":"abc","symbol":"ABC","symbol":"BTC"}]})","ABC"));
    assert(!parse(R"({"coins":[],"coins":[]})","ABC"));
    assert(!parse(R"({"coins":[])","ABC"));
    assert(!parse(R"({"coins":[],})","ABC"));
    assert(!parse("<html>429</html>","ABC"));
    assert(!parse(R"({"coins":[]} trailing)","ABC"));
    assert(parse(R"({"coins":[{"id":"abc","symbol":"A\u0042C","large":"https:\/\/assets.coingecko.com\/coins\/images\/1\/large\/a.png"}]})","ABC"));

    IconPixels pixels;
    pixels.width=7;
    constexpr char html[]="<html>not an image</html>";
    assert(!decode_icon(html,sizeof(html)-1,pixels)&&pixels.width==7);
    assert(!decode_icon("x",kIconResponseMax+1,pixels));
    assert(argc==2); // fixture directory passed by run-tests.sh
    for(const char *symbol:{"BTC","ETH","SOL","XRP","DOGE","USDT"}){
        const std::string file=std::string(argv[1])+"/"+symbol+".png";
        std::ifstream in(file,std::ios::binary);assert(in);
        const std::string data((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
        assert(decode_icon(data.data(),data.size(),pixels));
        assert(pixels.width==128&&pixels.height==128&&pixels.rgba.size()==128*128*4);
        assert(!decode_icon(data.data(),24,pixels));
        // PNG header claiming excessive dimensions must fail before allocation.
        std::string oversized=data;
        oversized[16]=1;oversized[17]=0;oversized[18]=0;oversized[19]=0;
        assert(!decode_icon(oversized.data(),oversized.size(),pixels));
    }
    std::cout<<"Market icon parsing, identity and image tests passed\n";
}
