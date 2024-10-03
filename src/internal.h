#ifndef WNP_INTERNALS_H
#define WNP_INTERNALS_H

#include "thread.h"
#include "wnp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

void __wnp_get_args(wnp_args_t* args_out);
int __wnp_start_update_cycle(wnp_player_t players_out[WNP_MAX_PLAYERS]);
int __wnp_add_player(wnp_player_t* player);
void __wnp_update_player(wnp_player_t* player);
void __wnp_remove_player(int player_id);
void __wnp_end_update_cycle();
bool __wnp_write_cover(int player_id, void* data, uint64_t size);
bool __wnp_get_cover_path(int player_id, char cover_path_out[WNP_STR_LEN]);
void __wnp_set_event_result(int event_id, wnp_event_result_t result);

typedef enum {
  WNP_CALLBACK_PLAYER_ADDED = 0,
  WNP_CALLBACK_PLAYER_UPDATED = 1,
  WNP_CALLBACK_PLAYER_REMOVED = 2,
  WNP_CALLBACK_ACTIVE_PLAYER_CHANGED = 3,
} _wnp_callback_type_t;

typedef enum {
  WNP_TRY_SET_STATE = 0,
  WNP_TRY_SKIP_PREVIOUS = 1,
  WNP_TRY_SKIP_NEXT = 2,
  WNP_TRY_SET_POSITION = 3,
  WNP_TRY_SET_VOLUME = 4,
  WNP_TRY_SET_RATING = 5,
  WNP_TRY_SET_REPEAT = 6,
  WNP_TRY_SET_SHUFFLE = 7,
} wnp_event_t;

#ifdef WNP_BUILD_PLATFORM_WEB
wnp_init_ret_t __wnp_platform_web_init();
void __wnp_platform_web_uninit();
void __wnp_platform_web_free(void* platform_data);
void __wnp_platform_web_event(wnp_player_t* player, wnp_event_t event, int event_id, int data);
#endif /* WNP_BUILD_PLATFORM_WEB */

#ifdef WNP_BUILD_PLATFORM_LINUX
wnp_init_ret_t __wnp_platform_linux_init();
void __wnp_platform_linux_uninit();
void __wnp_platform_linux_free(void* platform_data);
void __wnp_platform_linux_event(wnp_player_t* player, wnp_event_t event, int event_id, int data);
#endif /* WNP_BUILD_PLATFORM_LINUX */

#ifdef WNP_BUILD_PLATFORM_DARWIN
wnp_init_ret_t __wnp_platform_darwin_init();
void __wnp_platform_darwin_uninit();
void __wnp_platform_darwin_free(void* platform_data);
void __wnp_platform_darwin_event(wnp_player_t* player, wnp_event_t event, int event_id, int data);
#endif /* WNP_BUILD_PLATFORM_DARWIN */

#ifdef WNP_BUILD_PLATFORM_WINDOWS
wnp_init_ret_t __wnp_platform_windows_init();
void __wnp_platform_windows_uninit();
void __wnp_platform_windows_free(void* platform_data);
void __wnp_platform_windows_event(wnp_player_t* player, wnp_event_t event, int event_id, int data);
#endif /* WNP_BUILD_PLATFORM_WINDOWS */

#ifdef __cplusplus
}
#endif

#endif /* WNP_INTERNALS_H */
