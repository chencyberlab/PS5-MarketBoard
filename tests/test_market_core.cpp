#include "../src/market_core.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

int main(){
 using namespace market;
 Preferences p{};char b[2048]{};Preferences p2{};
 assert(serialize_prefs(p,b,sizeof(b)));
 assert(parse_prefs(b,std::strlen(b),p2));
 assert(p2.interval_index==1 && p2.count==3);
 assert(add_pair(p2,"  doge/usdt  "));
 assert(p2.count==4&&std::strcmp(p2.pairs[3],"DOGE/USDT")==0);
 assert(!add_pair(p2,"DOGE/USDT"));
 assert(!add_pair(p2,"BTC/EUR"));
 assert(!add_pair(p2,"DOGE&UNSAFE/USDT"));
 assert(remove_pair(p2,1)&&p2.count==3);
 p2.interval_index=4;
 assert(serialize_prefs(p2,b,sizeof(b)));
 Preferences p3{}; assert(parse_prefs(b,std::strlen(b),p3));
 assert(p3.interval_index==4&&p3.count==3);
 const char *invalid="MARKETBOARD 2\nINTERVAL 3\nPAIR BTC/USDT\n";
 assert(!parse_prefs(invalid,std::strlen(invalid),p3));
 const char *junk="MARKETBOARD 1\nINTERVAL 15junk\nPAIR BTC/USDT\n";
 assert(!parse_prefs(junk,std::strlen(junk),p3));
 Quote q{};
 const char *binance=R"({"symbol":"BTCUSDT","lastPrice":"61234.40000000","priceChangePercent":"-2.35"})";
 assert(parse_provider_price(Provider::Binance,binance,std::strlen(binance),"BTC",q));
 assert(std::fabs(q.price-61234.4)<0.001&&q.has_change&&q.change_24h<0);
 const char *kucoin=R"({"code":"200000","data":{"last":"62000.3","changeRate":"0.051"}})";
 assert(parse_provider_price(Provider::KuCoin,kucoin,std::strlen(kucoin),"BTC",q));
 assert(std::fabs(q.change_24h-5.1)<1e-4);
 const char *coinbase=R"({"open":"60000","last":"61200"})";
 assert(parse_provider_price(Provider::Coinbase,coinbase,std::strlen(coinbase),"BTC",q));
 assert(std::fabs(q.change_24h-2)<1e-4);
 const char *coingecko=R"({"bitcoin":{"usd":1000.50,"usd_24h_change":-5.5}})";
 assert(parse_provider_price(Provider::CoinGecko,coingecko,std::strlen(coingecko),"BTC",q));
 assert(q.price==1000.5);
 Quote saved=q;
 const char *html="<html>429 rate limit</html>";
 assert(!parse_provider_price(Provider::Coinbase,html,std::strlen(html),"BTC",q));
 assert(q.price==saved.price);
 assert(!parse_provider_price(Provider::Binance,R"({"lastPrice":"nan"})",20,"BTC",q));
 std::cout<<"Market core and preference tests passed\n";
}
