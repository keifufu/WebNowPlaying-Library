#ifdef WNP_BUILD_PLATFORM_LINUX
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"
#endif /* WNP_BUILD_PLATFORM_LINUX */
