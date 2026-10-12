// SPDX-License-Identifier: GPL-3.0-or-later
#include "market_icon_image.hpp"
#include <cstdlib>
#include <utility>

namespace {
// PNG's compressed stream can expand beyond its claimed pixel dimensions.
// Bound the decoder's scratch allocations as well as its input and output.
constexpr std::size_t kMaxIconAllocation=2*1024*1024;
void *icon_allocate(std::size_t size){return size<=kMaxIconAllocation?std::malloc(size):nullptr;}
void *icon_reallocate(void *data,std::size_t size){
    return size<=kMaxIconAllocation?std::realloc(data,size):nullptr;
}
}
#define STBI_MALLOC(size) icon_allocate(size)
#define STBI_REALLOC(data,size) icon_reallocate(data,size)
#define STBI_FREE(data) std::free(data)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_MAX_DIMENSIONS 256
#include "third_party/marketboard/stb_image.h"

namespace market {
bool decode_icon(const char *data,std::size_t size,IconPixels &out) noexcept {
    if(!data||size==0||size>kIconResponseMax)return false;
    const auto *bytes=reinterpret_cast<const unsigned char *>(data);
    int w=0,h=0,channels=0;
    if(!stbi_info_from_memory(bytes,static_cast<int>(size),&w,&h,&channels)||
       w<1||h<1||w>kIconMaxDimension||h>kIconMaxDimension)return false;
    unsigned char *decoded=stbi_load_from_memory(bytes,static_cast<int>(size),&w,&h,&channels,4);
    if(!decoded)return false;
    IconPixels next;
    next.width=w;next.height=h;
    next.rgba.assign(decoded,decoded+static_cast<std::size_t>(w)*static_cast<std::size_t>(h)*4);
    stbi_image_free(decoded);
    out=std::move(next);return true;
}
}
