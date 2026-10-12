// SPDX-License-Identifier: GPL-3.0-or-later
#include "market_icons.hpp"
#include "market_http.hpp"
#include "core/save_file.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <pthread.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

extern "C" int sceKernelUsleep(std::uint32_t);

namespace market {
namespace {
struct Slot {
    char base[kIconBaseSize]{};
    std::shared_ptr<const IconPixels> pixels;
    long long retry_at=0;
    bool local_checked=false;
};
pthread_mutex_t icon_mutex=PTHREAD_MUTEX_INITIALIZER;
Slot slots[kMaxPairs]{};
int count=0;
bool started=false;
// Configuration is read once on the worker, never from the render loop.
bool downloads=true;
bool cache_writable=false;
char demo_key[129]{};
long long api_retry_at=0;
int api_failures=0;

long long now_seconds() noexcept {return static_cast<long long>(std::time(nullptr));}
void configure(){
    cache_writable=hui::save::ensure_directory(kIconDataRoot)&&
                   hui::save::ensure_directory(kIconCacheRoot);
    std::string value;
    if(hui::save::read_file(std::string(kIconDataRoot)+"/icons-enabled.txt",&value,16)){
        downloads=value=="1"||value=="1\n"||value=="1\r\n";
    }
    if(hui::save::read_file(std::string(kIconDataRoot)+"/coingecko-demo-key.txt",&value,130)){
        while(!value.empty()&&(value.back()=='\n'||value.back()=='\r'))value.pop_back();
        bool valid=!value.empty()&&value.size()<sizeof(demo_key);
        for(unsigned char c:value)if(c<=32||c>=127)valid=false;
        if(valid)std::memcpy(demo_key,value.c_str(),value.size()+1);
    }
}
std::shared_ptr<const IconPixels> load(const char *path){
    std::string data;IconPixels pixels;
    if(!hui::save::read_file(path,&data,kIconResponseMax)||
       !decode_icon(data.data(),data.size(),pixels))return {};
    return std::make_shared<IconPixels>(std::move(pixels));
}
// Bounded persistent cache, evict oldest downloaded images only. Bundles
// under /app0 are read-only. No directory listing of /app0 is required.
void trim_cache(){
    struct File {std::string path;long long at;};
    std::vector<File> files;
    for(const std::string &name:hui::save::list_files(kIconCacheRoot)){
        if(name.size()<6||name.size()>14||name.compare(name.size()-4,4,".img")!=0)continue;
        const std::string base=name.substr(0,name.size()-4);
        if(!valid_icon_base(base.c_str()))continue;
        const std::string path=std::string(kIconCacheRoot)+"/"+name;
        struct stat info{};
        if(::stat(path.c_str(),&info)==0&&S_ISREG(info.st_mode))
            files.push_back({path,static_cast<long long>(info.st_mtime)});
    }
    std::sort(files.begin(),files.end(),[](const File &a,const File &b){return a.at<b.at;});
    for(std::size_t i=0;i+static_cast<std::size_t>(kIconCacheEntries)<files.size();++i)
        (void)::unlink(files[i].path.c_str());
}
void backoff(const HttpResponse &response){
    if(response.transport==0&&response.http_status==200){api_failures=0;return;}
    api_failures=std::min(api_failures+1,6);
    const int delay=std::max(30 << (api_failures-1),response.retry_after);
    api_retry_at=now_seconds()+std::min(delay,3600);
    if(response.http_status==401||response.http_status==403||response.http_status==451)
        api_retry_at=now_seconds()+3600;
}
std::shared_ptr<const IconPixels> download(const char *base){
    char url[1100]{};
    // Validated ASCII base symbols need no URL escaping.
    std::snprintf(url,sizeof(url),"https://api.coingecko.com/api/v3/search?query=%s",base);
    std::vector<char> data(kIconResponseMax+1);
    const HttpResponse search=get_https(url,data.data(),data.size(),demo_key);
    backoff(search);
    // Pace even successful metadata requests independently of price polling.
    api_retry_at=std::max(api_retry_at,now_seconds()+10);
    if(search.transport!=0||search.http_status!=200||
       !parse_icon_search(data.data(),search.used,base,url,sizeof(url)))return {};
    const HttpResponse image=get_https(url,data.data(),data.size());
    if(image.transport!=0||image.http_status!=200){backoff(image);return {};}
    IconPixels pixels;
    if(!decode_icon(data.data(),image.used,pixels))return {};
    // Only complete, successfully decoded images are ever persisted.
    char path[192]{};
    if(cache_writable&&icon_path(kIconCacheRoot,base,"img",path,sizeof(path))&&
       hui::save::write_atomic(path,std::string_view(data.data(),image.used)).empty())trim_cache();
    return std::make_shared<IconPixels>(std::move(pixels));
}
void *run(void *) noexcept {
    configure();
    if(cache_writable)trim_cache();
    for(;;){
        Slot task;bool found=false;
        const long long now=now_seconds();
        (void)pthread_mutex_lock(&icon_mutex);
        // Load every local image before scheduling network work.
        for(int i=0;i<count;++i)if(!slots[i].local_checked){task=slots[i];found=true;break;}
        if(!found&&downloads&&now>=api_retry_at){
            for(int i=0;i<count;++i)if(!slots[i].pixels&&now>=slots[i].retry_at){
                task=slots[i];found=true;break;
            }
        }
        (void)pthread_mutex_unlock(&icon_mutex);
        if(!found){(void)sceKernelUsleep(250000);continue;}
        std::shared_ptr<const IconPixels> pixels;
        if(!task.local_checked){
            char path[192]{};
            if(bundled_icon(task.base)&&icon_path(kIconAssetRoot,task.base,"png",path,sizeof(path)))
                pixels=load(path);
            if(!pixels&&icon_path(kIconCacheRoot,task.base,"img",path,sizeof(path)))pixels=load(path);
        }else pixels=download(task.base);
        (void)pthread_mutex_lock(&icon_mutex);
        for(int i=0;i<count;++i)if(std::strcmp(slots[i].base,task.base)==0){
            slots[i].local_checked=true;
            if(pixels)slots[i].pixels=std::move(pixels);
            // Missing/ambiguous/unsupported icons retry after 30 minutes.
            if(task.local_checked)slots[i].retry_at=now_seconds()+1800;
            break;
        }
        (void)pthread_mutex_unlock(&icon_mutex);
    }
    return nullptr;
}
}
void set_icon_preferences(const Preferences &prefs) noexcept {
    if(prefs.count<1||prefs.count>kMaxPairs)return;
    Slot updated[kMaxPairs]{};int used=0;
    (void)pthread_mutex_lock(&icon_mutex);
    for(int i=0;i<prefs.count;++i){
        char base[kIconBaseSize]{};if(!icon_base(prefs.pairs[i],base))continue;
        bool duplicate=false;
        for(int j=0;j<used;++j)if(std::strcmp(updated[j].base,base)==0)duplicate=true;
        if(duplicate)continue;
        for(int j=0;j<count;++j)if(std::strcmp(slots[j].base,base)==0){updated[used]=slots[j];break;}
        std::strcpy(updated[used++].base,base);
    }
    for(int i=0;i<kMaxPairs;++i)slots[i]=std::move(updated[i]);
    count=used;
    (void)pthread_mutex_unlock(&icon_mutex);
}
void start_icons(const Preferences &prefs) noexcept {
    if(started)return;
    started=true;set_icon_preferences(prefs);
    pthread_t thread;pthread_attr_t attr{};
    (void)pthread_attr_init(&attr);
    (void)pthread_attr_setstacksize(&attr,1024*1024);
    const int rc=pthread_create(&thread,&attr,run,nullptr);
    (void)pthread_attr_destroy(&attr);
    if(rc==0)(void)pthread_detach(thread);
}
std::shared_ptr<const IconPixels> read_icon(const char *base) noexcept {
    std::shared_ptr<const IconPixels> copy;
    if(!base)return copy;
    (void)pthread_mutex_lock(&icon_mutex);
    for(int i=0;i<count;++i)if(std::strcmp(slots[i].base,base)==0){copy=slots[i].pixels;break;}
    (void)pthread_mutex_unlock(&icon_mutex);
    return copy;
}
}
