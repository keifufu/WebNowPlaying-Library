// haha osx support
// wouldn't that be funny

#ifdef WNP_BUILD_PLATFORM_DARWIN
#include "internal.h"

wnp_init_ret_t __wnp_platform_darwin_init() {
  return WNP_INIT_SUCCESS;
}

void __wnp_platform_darwin_uninit() {
}

void __wnp_platform_darwin_free(void* platform_data) {
}

void __wnp_platform_darwin_event(wnp_player_t* player, wnp_event_t event, int event_id, int data) {
}

#endif /* WNP_BUILD_PLATFORM_DARWIN */
