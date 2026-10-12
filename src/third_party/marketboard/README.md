# stb_image

Unmodified `stb_image.h` from [nothings/stb](https://github.com/nothings/stb),
revision `2c980bb59875b0d32144a71867fbdebb2f77cd20`.
Its MIT/public-domain dual license is included at the end of the header.

`market_icon_image.cpp` provides the implementation, with only PNG and JPEG
enabled, no filesystem loading, no HDR/float conversion, and a 256-pixel
dimension limit. Individual decoder allocations are capped at 2 MiB, including
PNG decompression scratch space. No separate package installation or new link
library is needed.
