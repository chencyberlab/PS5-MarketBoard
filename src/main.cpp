// PS5 MarketBoard - native fullscreen crypto market dashboard.
// Uses the GPL-3.0-or-later ps5-homebrew-ui OpenGL renderer and platform kit.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "market_core.hpp"
#include "market_network.hpp"
#include "core/input.hpp"
#include "core/save_file.hpp"
#include "gfx/backdrop_spec.hpp"
#include "gfx/draw_list.hpp"
#include "gfx/renderer.hpp"
#include "platform/ps5/display_egl.hpp"
#include "platform/ps5/pad.hpp"
#include "platform/ps5/system.hpp"
#include "ui/fonts.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <span>
#include <string>

namespace {
using hui::gfx::Color;
using hui::gfx::Rect;
namespace g = hui::gfx;
namespace ui = hui::ui;

constexpr char kDataRoot[]="/download0/marketboard";
constexpr char kPrefsFile[]="/download0/marketboard/prefs.txt";
const Color midnight=Color::rgb(0x070b16);
const Color panel=Color::rgb(0x101b2e);
const Color panel2=Color::rgb(0x15223a);
const Color border=Color::rgb(0x263650);
const Color white=Color::rgb(0xf1f7ff);
const Color muted=Color::rgb(0x879ab7);
const Color dim=Color::rgb(0x526781);
const Color cyan=Color::rgb(0x52e7e0);
const Color violet=Color::rgb(0x8990ff);
const Color green=Color::rgb(0x5ae3a8);
const Color red=Color::rgb(0xff8392);

const char *const keys="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/"; // 37 glyphs
constexpr int key_count=39; // char keys + DEL + SAVE

std::string label_price(double v){
    char buf[64]{};
    if(v>=1000){
        char tmp[64]{};std::snprintf(tmp,sizeof(tmp),"%.2f",v);
        // insert grouping separators without relying on locale
        const char *dot=std::strchr(tmp,'.');int integer=dot?static_cast<int>(dot-tmp):static_cast<int>(std::strlen(tmp));
        std::string s;
        for(int i=0;i<integer;++i){if(i>0&&(integer-i)%3==0)s+=',';s+=tmp[i];}
        if(dot)s+=dot;
        return s;
    }
    if(v>=1)std::snprintf(buf,sizeof(buf),"%.2f",v);
    else if(v>=0.01)std::snprintf(buf,sizeof(buf),"%.4f",v);
    else std::snprintf(buf,sizeof(buf),"%.8f",v);
    return buf;
}
void text(g::DrawList &d,const ui::FontRef &font,const char *s,float x,float y,float size,
          Color color,g::Align align=g::Align::left){
    ui::text(d,font,s,x,y,size,color,align);
}
void box(g::DrawList &d,Rect r,float radius=25.0f){
    d.shadow(r,radius,34.0f,Color::rgb(0x000000,0.18f));
    d.gradient_rect(r,radius,panel2,panel);
    d.bordered_rect(r,radius,Color::rgb(0x0b1220,0.14f),1.2f,border);
}
void label(g::DrawList &d,const ui::FontRef &f,const char *s,float x,float y,Color c=muted){
    text(d,f,s,x,y,19,c);
}

void sparkline(g::DrawList &d,const market::Entry &entry,Rect r,Color tint){
    if(entry.history_len<2){
        d.line(r.x+8,r.y+r.h*.65f,r.x+r.w-8,r.y+r.h*.65f,2.0f,dim);
        return;
    }
    double minimum=entry.history[0], maximum=entry.history[0];
    for(int i=0;i<entry.history_len;++i){
        int idx=(entry.history_next-entry.history_len+i+market::kHistory)%market::kHistory;
        minimum=std::min(minimum,entry.history[idx]);maximum=std::max(maximum,entry.history[idx]);
    }
    double spread=maximum-minimum;
    if(spread<maximum*0.00001)spread=maximum*0.00001;
    if(spread<=0)spread=1;
    float last_x=0,last_y=0;
    for(int i=0;i<entry.history_len;++i){
        int idx=(entry.history_next-entry.history_len+i+market::kHistory)%market::kHistory;
        float x=r.x+6+(r.w-12)*static_cast<float>(i)/static_cast<float>(entry.history_len-1);
        float y=r.y+8+(r.h-16)*(1.0f-static_cast<float>((entry.history[idx]-minimum)/spread));
        if(i>0)d.line(last_x,last_y,x,y,3.0f,tint);
        last_x=x;last_y=y;
    }
    d.circle(last_x,last_y,5.5f,tint);
}

class Dashboard {
 public:
  explicit Dashboard(const ui::Fonts &fonts):fonts_(fonts){}
  void start(){
    hui::save::ensure_directory(kDataRoot);
    std::string saved;
    market::Preferences parsed{};
    if(hui::save::read_file(kPrefsFile,&saved,2048)&&
       market::parse_prefs(saved.data(),saved.size(),parsed))prefs_=parsed;
    market::start_market_updates(prefs_);
  }
  void update(const hui::InputFrame &input){
    if(keyboard_){ keyboard_input(input);return; }
    if(input.is_pressed(hui::Action::back)){quit_=true;return;}
    if(input.nav==hui::Direction::up&&selected_>0)--selected_;
    if(input.nav==hui::Direction::down&&selected_+1<prefs_.count)++selected_;
    if(input.is_pressed(hui::Action::page_prev)&&prefs_.interval_index>0){
        --prefs_.interval_index;save_and_update();
    }
    if(input.is_pressed(hui::Action::page_next)&&prefs_.interval_index+1<market::kIntervalCount){
        ++prefs_.interval_index;save_and_update();
    }
    if(input.is_pressed(hui::Action::menu)||input.is_pressed(hui::Action::confirm)){
        keyboard_=true;entry_text_[0]=0;key_cursor_=0;return;
    }
    if(input.is_pressed(hui::Action::north)){
        if(market::remove_pair(prefs_,selected_)){
            if(selected_>=prefs_.count)selected_=prefs_.count-1;
            save_and_update();notification_="Pair removed";notification_since_=elapsed_;
        }else {notification_="Keep at least one pair";notification_since_=elapsed_;}
    }
    if(input.is_pressed(hui::Action::west)){
        market::force_market_refresh();notification_="Refresh requested";notification_since_=elapsed_;
    }
  }
  bool should_quit() const {return quit_;}
  void render(g::Renderer &renderer,float elapsed){
    const market::FeedState feed=market::read_market_state();
    g::BackdropSpec backdrop{};
    backdrop.mode=g::BackdropMode::aurora;
    backdrop.colors[0]=Color::rgb(0x070b16);
    backdrop.colors[1]=Color::rgb(0x0b1330);
    backdrop.colors[2]=Color::rgb(0x12485a);
    backdrop.colors[3]=Color::rgb(0x292453);
    backdrop.time=elapsed*0.18f;
    g::DrawList d;
    d.clear();
    d.rounded_rect({83,74,9,48},4,cyan);
    text(d,fonts_.semibold,"MARKETBOARD",111,112,41,white);
    text(d,fonts_.regular,"PS5  /  DIGITAL MARKETS",112,146,17,muted);
    time_block(d);
    header_divider(d);
    big_quote(d,feed);
    watchlist(d,feed);
    bottom_cards(d,feed);
    footer(d,feed);
    if(keyboard_)draw_keyboard(d);
    if(!notification_.empty()&&elapsed-notification_since_<4.0f){
        d.rounded_rect({737,951,445,54},18,Color::rgb(0x0c2330));
        text(d,fonts_.semibold,notification_.c_str(),960,986,22,cyan,g::Align::center);
    }
    renderer.begin();renderer.backdrop(backdrop);renderer.draw(d);
    renderer.present(0,1920,1080); // consume draw-list before local lifetime ends
  }
 private:
  const ui::Fonts &fonts_;
  market::Preferences prefs_{};
  int selected_=0;
  bool keyboard_=false,quit_=false;
  char entry_text_[market::kSymbolSize]{};
  int key_cursor_=0;
  std::string notification_;
  float notification_since_=0;
  float elapsed_=0;

  void save_and_update(){
    char buffer[2048]{};
    if(market::serialize_prefs(prefs_,buffer,sizeof(buffer)))
       hui::save::write_atomic(kPrefsFile,buffer);
    market::set_market_preferences(prefs_);
  }
  void keyboard_input(const hui::InputFrame &in){
    if(in.is_pressed(hui::Action::back)){keyboard_=false;return;}
    int row=key_cursor_/10,col=key_cursor_%10;
    if(in.nav==hui::Direction::up)row=std::max(0,row-1);
    if(in.nav==hui::Direction::down)row=std::min(3,row+1);
    if(in.nav==hui::Direction::left)col=std::max(0,col-1);
    if(in.nav==hui::Direction::right)col=std::min(9,col+1);
    key_cursor_=std::min(row*10+col,key_count-1);
    if(in.is_pressed(hui::Action::north))backspace();
    if(in.is_pressed(hui::Action::menu)){confirm_pair();return;}
    if(!in.is_pressed(hui::Action::confirm))return;
    if(key_cursor_==37){backspace();return;}
    if(key_cursor_==38){confirm_pair();return;}
    std::size_t n=std::strlen(entry_text_);
    if(n<market::kSymbolSize-1){
        entry_text_[n]=keys[key_cursor_];entry_text_[n+1]=0;
    }
  }
  void backspace(){std::size_t n=std::strlen(entry_text_);if(n)entry_text_[n-1]=0;}
  void confirm_pair(){
    if(market::add_pair(prefs_,entry_text_)){
      selected_=prefs_.count-1;keyboard_=false;save_and_update();
      notification_="Pair added";
    }else notification_="Invalid, duplicate or watchlist full";
    notification_since_=elapsed_;
  }
  void header_divider(g::DrawList &d){d.line(84,174,1836,174,1.5f,border);}
  void time_block(g::DrawList &d){
    std::time_t stamp=std::time(nullptr);
    std::tm local{};
    char clock[40]="--:--",date[64]="DATE UNAVAILABLE";
    if(stamp>0&&localtime_r(&stamp,&local)){
        std::strftime(clock,sizeof(clock),"%H:%M:%S",&local);
        std::strftime(date,sizeof(date),"%a, %d %b %Y",&local);
    }
    text(d,fonts_.mono,clock,1834,108,40,white,g::Align::right);
    text(d,fonts_.regular,date,1834,145,19,muted,g::Align::right);
  }
  void big_quote(g::DrawList &d,const market::FeedState &feed){
    Rect r{83,206,1104,573};box(d,r,30);
    const auto &e=feed.entries[selected_];
    d.circle(139,268,9,(!e.ready?dim:(e.fresh?green:red)));
    text(d,fonts_.semibold,e.symbol,164,279,28,white);
    text(d,fonts_.regular,(!e.ready?"AWAITING MARKET DATA":
         e.fresh?"LIVE MARKET QUOTE":"LAST KNOWN QUOTE"),1134,279,16,muted,g::Align::right);
    text(d,fonts_.regular,"LATEST PRICE",133,363,21,muted);
    if(e.ready){
        const std::string price=label_price(e.quote.price);
        float size=price.size()>14?54.0f:price.size()>10?67.0f:84.0f;
        text(d,fonts_.semibold,price.c_str(),130,469,size,white);
        const char *quote=std::strchr(e.symbol,'/');
        text(d,fonts_.semibold,quote?quote+1:"",135,505,23,muted);
        if(e.quote.has_change){
            char change[48]{};
            std::snprintf(change,sizeof(change),"%+.2f%%   /   24H",e.quote.change_24h);
            d.rounded_rect({133,539,243,51},18,Color::rgb(e.quote.change_24h>=0?0x133c38:0x442631));
            text(d,fonts_.semibold,change,151,574,20,e.quote.change_24h>=0?green:red);
        }
    }else{
        text(d,fonts_.semibold,"--",133,470,83,white);
        text(d,fonts_.regular,"Connecting to available exchanges...",136,538,21,muted);
    }
    text(d,fonts_.semibold,"OBSERVED PRICE HISTORY",134,651,18,muted);
    text(d,fonts_.regular,"Updates collected while this app is open",1133,650,16,dim,g::Align::right);
    d.rounded_rect({126,671,1004,81},16,Color::rgb(0x0b1425));
    sparkline(d,e,{147,687,959,50},e.quote.change_24h<0?red:cyan);
    d.line(119,615,1154,615,1,border);
    if(e.updated_at>0){
        const std::time_t stamp=static_cast<std::time_t>(e.updated_at);
        std::tm local{};char last[56]{};
        if(localtime_r(&stamp,&local)){
            std::strftime(last,sizeof(last),"Updated %H:%M:%S",&local);
            text(d,fonts_.regular,last,384,574,16,muted);
        }
    }
    const char *source=e.ready?market::provider_name(e.provider):"SEARCHING";
    text(d,fonts_.regular,"SOURCE",842,572,16,muted);
    text(d,fonts_.semibold,source,1140,572,18,cyan,g::Align::right);
  }
  void watchlist(g::DrawList &d,const market::FeedState &feed){
    const Rect r{1216,206,621,746};box(d,r,30);
    text(d,fonts_.semibold,"WATCHLIST",1251,267,29,white);
    char count[28]{};std::snprintf(count,sizeof(count),"%02d / %02d",prefs_.count,market::kMaxPairs);
    text(d,fonts_.mono,count,1791,267,21,muted,g::Align::right);
    d.line(1243,294,1808,294,1,border);
    const int displayed=std::min(prefs_.count,9);
    int offset=0;
    if(selected_>=9)offset=selected_-8;
    for(int row=0;row<displayed;++row){
        const int idx=row+offset;
        const market::Entry &e=feed.entries[idx];
        const int y=313+row*66;
        if(idx==selected_){
            d.rounded_rect({1236,static_cast<float>(y-9),581,62},16,Color::rgb(0x223450));
            d.rounded_rect({1236,static_cast<float>(y-6),4,56},2,cyan);
        }
        text(d,fonts_.semibold,e.symbol,1262,y+17,23,idx==selected_?white:Color::rgb(0xcedbef));
        if(e.ready){
            const std::string price=label_price(e.quote.price);
            text(d,fonts_.mono,price.c_str(),1787,y+12,25,white,g::Align::right);
            if(e.quote.has_change){
                char change[36]{};
                std::snprintf(change,sizeof(change),"%+.2f%%",e.quote.change_24h);
                text(d,fonts_.regular,change,1787,y+38,17,e.quote.change_24h>=0?green:red,g::Align::right);
            }
        }else text(d,fonts_.regular,"Waiting...",1787,y+23,18,muted,g::Align::right);
    }
    text(d,fonts_.regular,"OPTIONS / ADD     TRIANGLE / REMOVE",1255,927,17,muted);
  }
  void bottom_cards(g::DrawList &d,const market::FeedState &feed){
    const Rect a{83,802,352,150},b{455,802,352,150},c{827,802,360,150};
    box(d,a,22);box(d,b,22);box(d,c,22);
    label(d,fonts_.regular,"AUTO REFRESH",109,842);
    char refresh[64]{};
    std::snprintf(refresh,sizeof(refresh),"%d seconds",market::kIntervals[prefs_.interval_index]);
    text(d,fonts_.semibold,refresh,109,906,38,white);
    label(d,fonts_.regular,"MARKET SOURCES",481,842);
    text(d,fonts_.semibold,"4 exchanges",481,906,32,white);
    label(d,fonts_.regular,"NEXT REFRESH",851,842);
    char next[48]{};
    const long long remaining=std::max(0LL,feed.next_cycle_at-static_cast<long long>(std::time(nullptr)));
    if(feed.updating)std::snprintf(next,sizeof(next),"Updating...");
    else std::snprintf(next,sizeof(next),"%llds",remaining);
    text(d,fonts_.semibold,next,851,906,34,white);
  }
  void footer(g::DrawList &d,const market::FeedState &feed){
    d.line(84,982,1836,982,1,border);
    text(d,fonts_.regular,"D-PAD  Navigate",89,1020,20,muted);
    text(d,fonts_.regular,"OPTIONS  Add pair",334,1020,20,muted);
    text(d,fonts_.regular,"TRIANGLE  Remove",585,1020,20,muted);
    text(d,fonts_.regular,"L1 / R1  Refresh rate",883,1020,20,muted);
    text(d,fonts_.regular,"SQUARE  Update now",1250,1020,20,muted);
    text(d,fonts_.regular,"CIRCLE  Exit",1579,1020,20,muted);
    const bool online=feed.last_success_at>0;
    d.circle(97,1053,5,online?green:dim);
    text(d,fonts_.regular,online?"Price feed active":"Waiting for live feed",114,1059,15,muted);
    text(d,fonts_.regular,"DATA: BINANCE / KUCOIN / COINBASE / COINGECKO",1833,1059,15,dim,g::Align::right);
  }
  void draw_keyboard(g::DrawList &d){
    d.rounded_rect({0,0,1920,1080},0,Color::rgb(0x050914,0.91f));
    Rect modal{424,182,1072,717};box(d,modal,35);
    text(d,fonts_.semibold,"ADD TRADING PAIR",479,256,34,white);
    text(d,fonts_.regular,"Examples: BTC/USDT, DOGE/USDT, ETH/USD",480,305,21,muted);
    d.rounded_rect({478,340,961,82},17,Color::rgb(0x0b1326));
    text(d,fonts_.mono,entry_text_[0]?entry_text_:"BASE/USDT",509,396,33,entry_text_[0]?white:dim);
    for(int i=0;i<key_count;++i){
        int row=i/10,col=i%10;
        const float x=479+col*97.0f,y=461+row*88.0f;
        const bool focused=key_cursor_==i;
        d.rounded_rect({x,y,85,72},14,focused?Color::rgb(0x21677b):Color::rgb(0x1a2a43));
        char glyph[8]{};
        if(i<37){glyph[0]=keys[i];glyph[1]=0;}
        else std::strcpy(glyph,i==37?"DEL":"OK");
        text(d,fonts_.semibold,glyph,x+42,y+46,i<37?27:20,focused?white:muted,g::Align::center);
    }
    text(d,fonts_.regular,"CROSS / TYPE     TRIANGLE / DELETE     OPTIONS / SAVE     CIRCLE / CANCEL",484,856,18,muted);
  }
 public:
  void set_elapsed(float seconds){elapsed_=seconds;}
};

bool load_font(g::Renderer &renderer,const char *path,g::Font &font,ui::FontRef &ref){
    std::string bytes;
    if(!hui::save::read_file(path,&bytes)||!font.load(bytes))return false;
    ref.font=&font;ref.texture=renderer.batch().create_font_texture(font);return true;
}
}

int main(){
    using namespace hui;
    ps5::Display display;
    if(!display.open(1920,1080)){sys::log("[MARKETBOARD] Display open failed");sys::park();}
    g::Renderer renderer;
    if(!renderer.init()){sys::log("[MARKETBOARD] Renderer init failed");sys::park();}
    g::Font f_regular,f_semibold,f_mono;
    ui::Fonts fonts;
    if(!load_font(renderer,"/app0/assets/fonts/inter-regular.huifont",f_regular,fonts.regular)||
       !load_font(renderer,"/app0/assets/fonts/inter-semibold.huifont",f_semibold,fonts.semibold)||
       !load_font(renderer,"/app0/assets/fonts/dejavu-sans-mono.huifont",f_mono,fonts.mono)){
        sys::log("[MARKETBOARD] Required fonts missing");sys::park();
    }
    ps5::Pad pad;pad.open();
    InputTracker tracker;
    Dashboard dashboard(fonts);dashboard.start();
    const auto launched=sys::monotonic_us();
    PadSample samples[64];
    bool first=true;
    for(;;){
        const auto now=sys::monotonic_us();
        dashboard.set_elapsed(static_cast<float>(now-launched)/1000000.0f);
        const std::size_t n=pad.read(samples);
        const InputFrame input=tracker.update(std::span<const PadSample>(samples,n),
                                               static_cast<std::uint64_t>(now));
        dashboard.update(input);
        if(dashboard.should_quit()){pad.close();sys::quit();}
        dashboard.render(renderer,static_cast<float>(now-launched)/1000000.0f);
        if(!display.swap()){sys::log("[MARKETBOARD] Display swap failed");sys::park();}
        if(first){sys::hide_splash_screen();first=false;}
    }
}
