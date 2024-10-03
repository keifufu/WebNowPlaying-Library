#include "wnp.h"
#include "internal.h"

/**
 * ================================
 * | Definitions and global state |
 * ================================
 */

typedef struct {
  wnp_player_t players[WNP_MAX_PLAYERS];
  thread_mutex_t players_lock;
  int total_web_players;
  wnp_event_result_t event_results[WNP_MAX_EVENT_RESULTS];
  thread_mutex_t event_results_lock;
  int active_player_id;
  wnp_args_t args;
  bool update_cycle;
  int update_cycle_added_players[WNP_MAX_PLAYERS];
  int update_cycle_updated_players[WNP_MAX_PLAYERS];
  wnp_player_t update_cycle_removed_players[WNP_MAX_PLAYERS];
  bool is_initialized;
} _wnp_state_t;

static _wnp_state_t _wnp_state = {0};

wnp_player_t WNP_DEFAULT_PLAYER = {
    .id = -1,
    .name = "default",
    .title = "",
    .artist = "",
    .album = "",
    .cover = "",
    .cover_src = "",
    .state = WNP_STATE_STOPPED,
    .position = 0,
    .duration = 0,
    .volume = 100,
    .rating = 0,
    .repeat = WNP_REPEAT_NONE,
    .shuffle = false,
    .rating_system = WNP_RATING_SYSTEM_NONE,
    .available_repeat = WNP_REPEAT_NONE,
    .can_set_state = false,
    .can_skip_previous = false,
    .can_skip_next = false,
    .can_set_position = false,
    .can_set_volume = false,
    .can_set_rating = false,
    .can_set_repeat = false,
    .can_set_shuffle = false,
    .created_at = 0,
    .updated_at = 0,
    .active_at = 0,
    .is_web_browser = false,
    .platform = WNP_PLATFORM_NONE,
    ._platform_data = NULL,
};

/**
 * ==============================
 * | Private internal functions |
 * ==============================
 */

static void _wnp_callback(int type, int player_id) {
  // Exception for ACTIVE_PLAYER_CHANGED, where the player can be NULL.
  if (type == WNP_CALLBACK_ACTIVE_PLAYER_CHANGED && player_id == -1) {
    if (_wnp_state.args.on_active_player_changed != NULL) {
      _wnp_state.args.on_active_player_changed(NULL, _wnp_state.args.callback_data);
    }
    return;
  }

  wnp_player_t player = WNP_DEFAULT_PLAYER;
  if (!wnp_get_player(player_id, &player)) {
    return;
  }

  switch (type) {
    case WNP_CALLBACK_PLAYER_ADDED: {
      if (_wnp_state.args.on_player_added != NULL) {
        _wnp_state.args.on_player_added(&player, _wnp_state.args.callback_data);
      }
      break;
    }
    case WNP_CALLBACK_PLAYER_UPDATED: {
      if (_wnp_state.args.on_player_updated != NULL) {
        _wnp_state.args.on_player_updated(&player, _wnp_state.args.callback_data);
      }
      break;
    }
    case WNP_CALLBACK_PLAYER_REMOVED: {
      if (_wnp_state.args.on_player_removed != NULL) {
        _wnp_state.args.on_player_removed(&player, _wnp_state.args.callback_data);
      }
      break;
    }
    case WNP_CALLBACK_ACTIVE_PLAYER_CHANGED: {
      if (_wnp_state.args.on_active_player_changed != NULL) {
        _wnp_state.args.on_active_player_changed(&player, _wnp_state.args.callback_data);
      }
      break;
    }
  }
}

static void _wnp_recalculate_active_player() {
  int active_player_id = -1;
  uint64_t max_active_at = 0;
  bool found_active = true;

  thread_mutex_lock(&_wnp_state.players_lock);
  for (size_t i = 0; i < WNP_MAX_PLAYERS; i++) {
    if (_wnp_state.total_web_players > 0 && _wnp_state.players[i].is_web_browser) continue;
    wnp_player_t* cur = &_wnp_state.players[i];
    if (cur->state == WNP_STATE_PLAYING && cur->volume != 0 && cur->active_at > max_active_at) {
      active_player_id = cur->id;
      max_active_at = cur->active_at;
      found_active = true;
    } else if (cur->state == WNP_STATE_PLAYING && !found_active) {
      active_player_id = cur->id;
      max_active_at = cur->active_at;
    } else if (cur->state != WNP_STATE_PLAYING && cur->active_at > max_active_at && !found_active) {
      bool is_active_playing = active_player_id == -1 ? false : _wnp_state.players[active_player_id].state == WNP_STATE_PLAYING;
      if (!is_active_playing) {
        active_player_id = cur->id;
        max_active_at = cur->active_at;
      }
    }
  }
  thread_mutex_unlock(&_wnp_state.players_lock);

  if (active_player_id != _wnp_state.active_player_id) {
    _wnp_state.active_player_id = active_player_id;
    _wnp_callback(WNP_CALLBACK_ACTIVE_PLAYER_CHANGED, active_player_id);
  }
}

static bool _wnp_is_adapter_version_valid(const char* adapter_version) {
  if (adapter_version == NULL) return false;

  const char* p = adapter_version;
  int dots = 0;
  int nums = 0;
  int other = 0;
  bool consecutive = false;

  while (*p != '\0') {
    if (*p == '.') {
      dots++;
      consecutive = false;
    } else if (*p >= 48 && *p <= 57) {
      nums++;
      if (!consecutive) {
        consecutive = true;
      } else {
        return false;
      }
    } else {
      other++;
    }
    p++;
  }

  return (dots == 2 && nums == 3 && other == 0);
}

static int _wnp_get_all_players_lockable(wnp_player_t players_out[WNP_MAX_PLAYERS], bool skip_browsers, bool lock) {
  int count = 0;

  if (lock) thread_mutex_lock(&_wnp_state.players_lock);
  for (size_t i = 0; i < WNP_MAX_PLAYERS; i++) {
    if (_wnp_state.players[i].id != -1) {
      if (skip_browsers && _wnp_state.total_web_players > 0 && _wnp_state.players[i].is_web_browser) continue;
      if (players_out != NULL) {
        players_out[count] = _wnp_state.players[i];
      }
      count++;
    }
  }
  if (lock) thread_mutex_unlock(&_wnp_state.players_lock);

  return count;
}

void _wnp_free_platform_data(wnp_player_t* player) {
  if (player->_platform_data == NULL) {
    return;
  }

  switch (player->platform) {
#ifdef WNP_BUILD_PLATFORM_WEB
    case WNP_PLATFORM_WEB: {
      __wnp_platform_web_free(player->_platform_data);
      break;
    }
#endif /* WNP_BUILD_PLATFORM_WEB */
#ifdef WNP_BUILD_PLATFORM_LINUX
    case WNP_PLATFORM_LINUX: {
      __wnp_platform_linux_free(player->_platform_data);
      break;
    }
#endif /* WNP_BUILD_PLATFORM_LINUX */
#ifdef WNP_BUILD_PLATFORM_DARWIN
    case WNP_PLATFORM_DARWIN: {
      __wnp_platform_darwin_free(player->_platform_data);
      break;
    }
#endif /* WNP_BUILD_PLATFORM_DARWIN */
#ifdef WNP_BUILD_PLATFORM_WINDOWS
    case WNP_PLATFORM_WINDOWS: {
      __wnp_platform_windows_free(player->_platform_data);
      break;
    }
#endif /* WNP_BUILD_PLATFORM_WINDOWS */
    default:
      break;
  }

  player->_platform_data = NULL;
}

/**
 * =============================
 * | Shared internal functions |
 * =============================
 */

void __wnp_get_args(wnp_args_t* args_out) {
  *args_out = _wnp_state.args;
}

int __wnp_start_update_cycle(wnp_player_t players_out[WNP_MAX_PLAYERS]) {
  thread_mutex_lock(&_wnp_state.players_lock);
  _wnp_state.update_cycle = true;
  return _wnp_get_all_players_lockable(players_out, false, false);
}

int __wnp_add_player(wnp_player_t* player) {
  if (!_wnp_state.update_cycle) return -1;

  int player_id = -1;

  for (size_t i = 0; i < WNP_MAX_PLAYERS; i++) {
    if (_wnp_state.players[i].id == -1) {
      player_id = i;
      _wnp_state.players[i] = *player;
      _wnp_state.players[i].id = player_id;
      break;
    }
  }

  if (player_id != -1) {
    if (player->platform == WNP_PLATFORM_WEB) {
      _wnp_state.total_web_players += 1;
    }

    for (size_t i = 0; i < WNP_MAX_PLAYERS; i++) {
      if (_wnp_state.update_cycle_added_players[i] == -1) {
        _wnp_state.update_cycle_added_players[i] = player_id;
        break;
      }
    }
  }

  return player_id;
}

void __wnp_update_player(wnp_player_t* player) {
  if (!_wnp_state.update_cycle) return;

  if (player->id < 0 || player->id >= WNP_MAX_PLAYERS || _wnp_state.players[player->id].id != player->id) {
    return;
  }

  _wnp_state.players[player->id] = *player;

  bool exists = false;
  for (size_t i = 0; i < WNP_MAX_PLAYERS; i++) {
    if (_wnp_state.update_cycle_added_players[i] == player->id || _wnp_state.update_cycle_updated_players[i] == player->id) {
      exists = true;
      break;
    }
  }

  if (!exists) {
    for (size_t i = 0; i < WNP_MAX_PLAYERS; i++) {
      if (_wnp_state.update_cycle_updated_players[i] == -1) {
        _wnp_state.update_cycle_updated_players[i] = player->id;
        break;
      }
    }
  }
}

void __wnp_remove_player(int player_id) {
  if (!_wnp_state.update_cycle) return;

  if (player_id < 0 || player_id >= WNP_MAX_PLAYERS) {
    return;
  }

  wnp_player_t* player = &_wnp_state.players[player_id];
  if (player->id != player_id) return;

  if (player->platform == WNP_PLATFORM_WEB) {
    _wnp_state.total_web_players -= 1;
  }

  _wnp_free_platform_data(player);

  for (size_t i = 0; i < WNP_MAX_PLAYERS; i++) {
    if (_wnp_state.update_cycle_removed_players[i].id != -1) {
      _wnp_state.update_cycle_removed_players[i] = *player;
      break;
    }
  }

  _wnp_state.players[player_id] = WNP_DEFAULT_PLAYER;
}

void __wnp_end_update_cycle() {
  _wnp_state.update_cycle = false;
  thread_mutex_unlock(&_wnp_state.players_lock);

  for (size_t i = 0; i < WNP_MAX_PLAYERS; i++) {
    int added_player_id = _wnp_state.update_cycle_added_players[i];
    if (added_player_id != -1) {
      _wnp_callback(WNP_CALLBACK_PLAYER_ADDED, added_player_id);
      _wnp_state.update_cycle_added_players[i] = -1;
    }
  }

  for (size_t i = 0; i < WNP_MAX_PLAYERS; i++) {
    int updated_player_id = _wnp_state.update_cycle_updated_players[i];
    if (updated_player_id != -1) {
      _wnp_callback(WNP_CALLBACK_PLAYER_UPDATED, updated_player_id);
      _wnp_state.update_cycle_updated_players[i] = -1;
    }
  }

  for (size_t i = 0; i < WNP_MAX_PLAYERS; i++) {
    wnp_player_t* removed_player = &_wnp_state.update_cycle_removed_players[i];
    if (removed_player->id != -1) {
      if (_wnp_state.args.on_player_removed != NULL) {
        _wnp_state.args.on_player_removed(removed_player, _wnp_state.args.callback_data);
      }
      _wnp_state.update_cycle_removed_players[i] = WNP_DEFAULT_PLAYER;
    }
  }

  _wnp_recalculate_active_player();
}

bool __wnp_get_cover_path(int player_id, char cover_path_out[WNP_STR_LEN]) {
  char file_name[WNP_STR_LEN] = {0};
  snprintf(file_name, WNP_STR_LEN, "libwnp-cover-%d.png", player_id);

#if defined(__linux__) || defined(__APPLE__)
  const char* tmp_dir = getenv("TMPDIR");
  if (!tmp_dir) {
    tmp_dir = "/tmp";
  }
  snprintf(cover_path_out, WNP_STR_LEN, "file://%s/%s", tmp_dir, file_name);
#elif _WIN32
  char* tmp = getenv("TEMP");
  if (tmp == NULL || strlen(tmp) + strlen(file_name) + 2 > WNP_STR_LEN) {
    return false;
  }

  for (size_t i = 0; tmp[i] != '\0'; i++) {
    if (tmp[i] == '\\') {
      tmp[i] = '/';
    }
  }
  snprintf(cover_path_out, WNP_STR_LEN, "file://%s/%s", tmp, file_name);
#else
  // Fallback for unknown systems, just use /tmp/
  snprintf(cover_path_out, WNP_STR_LEN, "file:///tmp/%s", file_name);
#endif

  return true;
}

bool __wnp_write_cover(int player_id, void* data, uint64_t size) {
  char file_path[WNP_STR_LEN] = {0};
  if (!__wnp_get_cover_path(player_id, file_path)) {
    return false;
  }

  FILE* file = fopen(file_path + 7, "wb");
  if (file == NULL) {
    return false;
  }
  size_t bytes_written = fwrite(data, sizeof(unsigned char), size, file);
  if (bytes_written != size) {
    fclose(file);
    return false;
  }
  fclose(file);
  return true;
}

void __wnp_set_event_result(int event_id, wnp_event_result_t result) {
  thread_mutex_lock(&_wnp_state.event_results_lock);
  _wnp_state.event_results[event_id] = result;
  thread_mutex_unlock(&_wnp_state.event_results_lock);
}

/**
 * ============================
 * | Internal event functions |
 * ============================
 */

static int _wnp_get_next_event_id() {
  static int event_id = 0;
  event_id = (event_id + 1) % WNP_MAX_EVENT_RESULTS;
  __wnp_set_event_result(event_id, WNP_EVENT_PENDING);
  return event_id;
}

static int _wnp_failed_event() {
  int event_id = _wnp_get_next_event_id();
  __wnp_set_event_result(event_id, WNP_EVENT_FAILED);
  return event_id;
}

static int _wnp_execute_event(int player_id, wnp_event_t event, int data) {
  if (player_id < 0 || player_id >= WNP_MAX_PLAYERS) {
    return _wnp_failed_event();
  }

  thread_mutex_lock(&_wnp_state.players_lock);
  wnp_player_t* player = &_wnp_state.players[player_id];
  if (player->id != player_id) {
    thread_mutex_unlock(&_wnp_state.players_lock);
    return _wnp_failed_event();
  }

  int ret = -1;
  switch (event) {
    case WNP_TRY_SET_STATE:
      if (!player->can_set_state) ret = _wnp_failed_event();
      break;
    case WNP_TRY_SKIP_PREVIOUS:
      if (!player->can_skip_previous) ret = _wnp_failed_event();
      break;
    case WNP_TRY_SKIP_NEXT:
      if (!player->can_skip_next) ret = _wnp_failed_event();
      break;
    case WNP_TRY_SET_POSITION:
      if (!player->can_set_position) ret = _wnp_failed_event();
      break;
    case WNP_TRY_SET_VOLUME:
      if (!player->can_set_volume) ret = _wnp_failed_event();
      break;
    case WNP_TRY_SET_RATING:
      if (!player->can_set_rating) ret = _wnp_failed_event();
      break;
    case WNP_TRY_SET_REPEAT:
      if (!player->can_set_repeat) ret = _wnp_failed_event();
      break;
    case WNP_TRY_SET_SHUFFLE:
      if (!player->can_set_shuffle) ret = _wnp_failed_event();
      break;
  }

  if (ret != -1) {
    thread_mutex_unlock(&_wnp_state.players_lock);
    return ret;
  }

  int event_id = _wnp_get_next_event_id();

  switch (player->platform) {
#ifdef WNP_BUILD_PLATFORM_WEB
    case WNP_PLATFORM_WEB: {
      __wnp_platform_web_event(player, event, event_id, data);
      break;
    }
#endif /* WNP_BUILD_PLATFORM_WEB */
#ifdef WNP_BUILD_PLATFORM_LINUX
    case WNP_PLATFORM_LINUX: {
      __wnp_platform_linux_event(player, event, event_id, data);
      break;
    }
#endif /* WNP_BUILD_PLATFORM_LINUX */
#ifdef WNP_BUILD_PLATFORM_DARWIN
    case WNP_PLATFORM_DARWIN: {
      __wnp_platform_darwin_event(player, event, event_id, data);
      break;
    }
#endif /* WNP_BUILD_PLATFORM_DARWIN */
#ifdef WNP_BUILD_PLATFORM_WINDOWS
    case WNP_PLATFORM_WINDOWS: {
      __wnp_platform_windows_event(player, event, event_id, data);
      break;
    }
#endif /* WNP_BUILD_PLATFORM_WINDOWS */
    default:
      break;
  }

  thread_mutex_unlock(&_wnp_state.players_lock);
  _wnp_callback(WNP_CALLBACK_PLAYER_UPDATED, player_id);
  return event_id;
}

/**
 * ===============================
 * | Public management functions |
 * ===============================
 */

wnp_init_ret_t wnp_init(wnp_args_t* args) {
  // clang-format off
#define WNP_PLATFORM_INIT(platform) \
  do { \
    uninit_functions[uninit_num] = __wnp_platform_##platform##_uninit; \
    uninit_num++; \
    int platform##_retval = __wnp_platform_##platform##_init(); \
    if (platform##_retval != WNP_INIT_SUCCESS) { \
      retval = platform##_retval; \
    } \
  } while (0)
  // clang-format on

  if (_wnp_state.is_initialized) {
    return WNP_INIT_ALREADY_INITIALIZED;
  }

  if (!_wnp_is_adapter_version_valid(args->adapter_version)) {
    return WNP_INIT_INVALID_ADAPTER_VERSION;
  }

  /* init state */
  for (size_t i = 0; i < WNP_MAX_PLAYERS; i++) {
    _wnp_state.players[i] = WNP_DEFAULT_PLAYER;
  }
  thread_mutex_init(&_wnp_state.players_lock);
  _wnp_state.total_web_players = 0;
  for (size_t i = 0; i < WNP_MAX_EVENT_RESULTS; i++) {
    _wnp_state.event_results[i] = WNP_EVENT_FAILED;
  }
  thread_mutex_init(&_wnp_state.event_results_lock);
  _wnp_state.active_player_id = -1;
  _wnp_state.args = *args;
  _wnp_state.update_cycle = false;
  for (size_t i = 0; i < WNP_MAX_PLAYERS; i++) {
    _wnp_state.update_cycle_added_players[i] = -1;
    _wnp_state.update_cycle_updated_players[i] = -1;
    _wnp_state.update_cycle_removed_players[i] = WNP_DEFAULT_PLAYER;
  }
  /* end init state */

  void (*uninit_functions[4])() = {0};
  int uninit_num = 0;
  wnp_init_ret_t retval = WNP_INIT_SUCCESS;

#ifdef WNP_BUILD_PLATFORM_WEB
  WNP_PLATFORM_INIT(web);
#endif /* WNP_BUILD_PLATFORM_WEB */
#ifdef WNP_BUILD_PLATFORM_LINUX
  WNP_PLATFORM_INIT(linux);
#endif /* WNP_BUILD_PLATFORM_LINUX */
#ifdef WNP_BUILD_PLATFORM_DARWIN
  WNP_PLATFORM_INIT(darwin);
#endif /* WNP_BUILD_PLATFORM_DARWIN */
#ifdef WNP_BUILD_PLATFORM_WINDOWS
  WNP_PLATFORM_INIT(windows);
#endif /* WNP_BUILD_PLATFORM_WINDOWS */

  if (retval == WNP_INIT_SUCCESS) {
    _wnp_state.is_initialized = true;
  } else {
    for (size_t i = 0; i < uninit_num; i++) {
      uninit_functions[i]();
    }
  }

  return retval;
}

void wnp_uninit() {
  if (!_wnp_state.is_initialized) {
    return;
  }

  /* cleanup state */
  for (size_t i = 0; i < WNP_MAX_PLAYERS; i++) {
    __wnp_start_update_cycle(NULL);
    __wnp_remove_player(_wnp_state.players[i].id);
    __wnp_end_update_cycle();
  }
  thread_mutex_term(&_wnp_state.players_lock);
  _wnp_state.total_web_players = 0;
  for (size_t i = 0; i < WNP_MAX_EVENT_RESULTS; i++) {
    _wnp_state.event_results[i] = WNP_EVENT_PENDING;
  }
  thread_mutex_term(&_wnp_state.event_results_lock);
  _wnp_state.active_player_id = -1;
  memset(&_wnp_state.args, 0, sizeof(wnp_args_t));
  _wnp_state.update_cycle = false;
  for (size_t i = 0; i < WNP_MAX_PLAYERS; i++) {
    _wnp_state.update_cycle_added_players[i] = -1;
    _wnp_state.update_cycle_updated_players[i] = -1;
    _wnp_state.update_cycle_removed_players[i] = WNP_DEFAULT_PLAYER;
  }
  /* end cleanup state */

#ifdef WNP_BUILD_PLATFORM_WEB
  __wnp_platform_web_uninit();
#endif /* WNP_BUILD_PLATFORM_WEB */
#ifdef WNP_BUILD_PLATFORM_LINUX
  __wnp_platform_linux_uninit();
#endif /* WNP_BUILD_PLATFORM_LINUX */
#ifdef WNP_BUILD_PLATFORM_DARWIN
  __wnp_platform_darwin_uninit();
#endif /* WNP_BUILD_PLATFORM_DARWIN */
#ifdef WNP_BUILD_PLATFORM_WINDOWS
  __wnp_platform_windows_uninit();
#endif /* WNP_BUILD_PLATFORM_WINDOWS */
}

bool wnp_is_initialized() {
  return _wnp_state.is_initialized;
}

/**
 * ===========================
 * | Public player functions |
 * ===========================
 */

bool wnp_get_player(int player_id, wnp_player_t* player_out) {
  if (player_id < 0 || player_id >= WNP_MAX_PLAYERS) {
    return false;
  }

  thread_mutex_lock(&_wnp_state.players_lock);
  wnp_player_t* player = &_wnp_state.players[player_id];
  if (player->id != player_id || (_wnp_state.total_web_players > 0 && player->is_web_browser)) {
    thread_mutex_unlock(&_wnp_state.players_lock);
    return false;
  }
  thread_mutex_unlock(&_wnp_state.players_lock);

  *player_out = *player;
  return true;
}

bool wnp_get_active_player(wnp_player_t* player_out) {
  if (_wnp_state.active_player_id == -1) {
    return false;
  }

  wnp_get_player(_wnp_state.active_player_id, player_out);
  return true;
}

int wnp_get_all_players(wnp_player_t players_out[WNP_MAX_PLAYERS]) {
  return _wnp_get_all_players_lockable(players_out, true, true);
}

/**
 * ============================
 * | Public utility functions |
 * ============================
 */

float wnp_get_position_percent(wnp_player_t* player) {
  if (player->duration == 0) return 100.0;
  return ((float)(player->position) / player->duration) * 100.0;
}

int wnp_get_remaining_seconds(wnp_player_t* player) {
  return player->duration - player->position;
}

void wnp_format_seconds(int seconds, bool pad_with_zeroes, char out_str[13]) {
  int hours = seconds / 3600;
  int remainder = seconds % 3600;
  int minutes = remainder / 60;
  int secs = remainder % 60;

  if (hours >= 1) {
    if (pad_with_zeroes) {
      snprintf(out_str, 13, "%02d:%02d:%02d", hours, minutes, secs);
    } else {
      snprintf(out_str, 13, "%d:%02d:%02d", hours, minutes, secs);
    }
  } else {
    if (pad_with_zeroes) {
      snprintf(out_str, 13, "%02d:%02d", minutes, secs);
    } else {
      snprintf(out_str, 13, "%d:%02d", minutes, secs);
    }
  }
}

void wnp_utf8_to_utf16(unsigned char* input, int input_len, uint16_t* output, int output_len) {
  unsigned char* utf8_currentCodeUnit = input;
  uint16_t* utf16_currentCodeUnit = output;
  int utf8_str_iterator = 0;
  int utf16_str_iterator = 0;
  while (*utf8_currentCodeUnit && (utf16_str_iterator < output_len || utf8_str_iterator < input_len)) {
    if (*utf8_currentCodeUnit < 0x80) {
      *utf16_currentCodeUnit = (uint16_t)(*utf8_currentCodeUnit);
      utf16_currentCodeUnit++;
      utf16_str_iterator++;
      utf8_currentCodeUnit++;
      utf8_str_iterator++;
    } else if (*utf8_currentCodeUnit < 0xC0) {
      utf8_currentCodeUnit++;
      utf8_str_iterator++;
    } else if (*utf8_currentCodeUnit < 0xE0) {
      uint16_t highShort = (uint16_t)((*utf8_currentCodeUnit) & 0x1F);
      utf8_currentCodeUnit++;
      uint16_t lowShort = (uint16_t)((*utf8_currentCodeUnit) & 0x3F);
      utf8_currentCodeUnit++;
      int unicode = (highShort << 6) | lowShort;
      if ((0 <= unicode && unicode <= 0xD7FF) || (0xE000 <= unicode && unicode <= 0xFFFF)) {
        *utf16_currentCodeUnit = (uint16_t)unicode;
        utf16_currentCodeUnit++;
        utf16_str_iterator++;
      }
      utf8_str_iterator += 2;
    } else if (*utf8_currentCodeUnit < 0xF0) {
      uint16_t fourthChar = (uint16_t)((*utf8_currentCodeUnit) & 0xF);
      utf8_currentCodeUnit++;
      uint16_t thirdChar = (uint16_t)((*utf8_currentCodeUnit) & 0x3C) >> 2;
      uint16_t secondCharHigh = (uint16_t)((*utf8_currentCodeUnit) & 0x3);
      utf8_currentCodeUnit++;
      uint16_t secondCharLow = (uint16_t)((*utf8_currentCodeUnit) & 0x30) >> 4;
      uint16_t firstChar = (uint16_t)((*utf8_currentCodeUnit) & 0xF);
      utf8_currentCodeUnit++;
      int unicode = (fourthChar << 12) | (thirdChar << 8) | (secondCharHigh << 6) | (secondCharLow << 4) | firstChar;
      if ((0 <= unicode && unicode <= 0xD7FF) || (0xE000 <= unicode && unicode <= 0xFFFF)) {
        *utf16_currentCodeUnit = (uint16_t)unicode;
        utf16_currentCodeUnit++;
        utf16_str_iterator++;
      }
      utf8_str_iterator += 3;
    } else if (*utf8_currentCodeUnit < 0xF8) {
      uint16_t sixthChar = (uint16_t)((*utf8_currentCodeUnit) & 0x4) >> 2;
      uint16_t fifthCharHigh = (uint16_t)((*utf8_currentCodeUnit) & 0x3);
      utf8_currentCodeUnit++;
      uint16_t fifthCharLow = (uint16_t)((*utf8_currentCodeUnit) & 0x30) >> 4;
      uint16_t fourthChar = (uint16_t)((*utf8_currentCodeUnit) & 0xF);
      utf8_currentCodeUnit++;
      uint16_t thirdChar = (uint16_t)((*utf8_currentCodeUnit) & 0x3C) >> 2;
      uint16_t secondCharHigh = (uint16_t)((*utf8_currentCodeUnit) & 0x3);
      utf8_currentCodeUnit++;
      uint16_t secondCharLow = (uint16_t)((*utf8_currentCodeUnit) & 0x30) >> 4;
      uint16_t firstChar = (uint16_t)((*utf8_currentCodeUnit) & 0xF);
      utf8_currentCodeUnit++;
      int unicode = (sixthChar << 4) | (fifthCharHigh << 2) | fifthCharLow | (fourthChar << 12) | (thirdChar << 8) | (secondCharHigh << 6) |
                    (secondCharLow << 4) | firstChar;
      uint16_t highSurrogate = (unicode - 0x10000) / 0x400 + 0xD800;
      uint16_t lowSurrogate = (unicode - 0x10000) % 0x400 + 0xDC00;
      *utf16_currentCodeUnit = lowSurrogate;
      utf16_currentCodeUnit++;
      utf16_str_iterator++;
      if (utf16_str_iterator < output_len) {
        *utf16_currentCodeUnit = highSurrogate;
        utf16_currentCodeUnit++;
        utf16_str_iterator++;
      }
      utf8_str_iterator += 4;
    } else {
      utf8_currentCodeUnit++;
      utf8_str_iterator++;
    }
  }
  while (utf16_str_iterator < output_len) {
    *utf16_currentCodeUnit = '\0';
    utf16_currentCodeUnit++;
    utf16_str_iterator++;
  }
}

/**
 * =================================
 * | Public event result functions |
 * =================================
 */

wnp_event_result_t wnp_get_event_result(int event_id) {
  if (event_id < 0 || event_id >= WNP_MAX_EVENT_RESULTS) {
    return WNP_EVENT_FAILED;
  }

  thread_mutex_lock(&_wnp_state.event_results_lock);
  int event_result = _wnp_state.event_results[event_id];
  thread_mutex_unlock(&_wnp_state.event_results_lock);

  return event_result;
}

wnp_event_result_t wnp_wait_for_event_result(int event_id) {
  if (event_id < 0 || event_id >= WNP_MAX_EVENT_RESULTS) {
    return WNP_EVENT_FAILED;
  }

  int count = 0;
  int max_count = 100;
  thread_timer_t timer;
  thread_timer_init(&timer);

  while (wnp_get_event_result(event_id) == WNP_EVENT_PENDING) {
    if (count > max_count) {
      __wnp_set_event_result(event_id, WNP_EVENT_FAILED);
      break;
    }
    count++;
    thread_timer_wait(&timer, 10 * 1000000); // 10ms
  }

  thread_timer_term(&timer);
  return wnp_get_event_result(event_id);
}

/**
 * ===============================
 * | Public base event functions |
 * ===============================
 */

int wnp_try_set_state(wnp_player_t* player, wnp_state_t state) {
  return _wnp_execute_event(player->id, WNP_TRY_SET_STATE, state);
}

int wnp_try_skip_previous(wnp_player_t* player) {
  return _wnp_execute_event(player->id, WNP_TRY_SKIP_PREVIOUS, 0);
}

int wnp_try_skip_next(wnp_player_t* player) {
  return _wnp_execute_event(player->id, WNP_TRY_SKIP_NEXT, 0);
}

int wnp_try_set_position(wnp_player_t* player, unsigned int seconds) {
  if (seconds > player->duration) seconds = player->duration;
  return _wnp_execute_event(player->id, WNP_TRY_SET_POSITION, seconds);
}

int wnp_try_set_volume(wnp_player_t* player, unsigned int volume) {
  if (volume > 100) volume = 100;
  return _wnp_execute_event(player->id, WNP_TRY_SET_VOLUME, volume);
}

int wnp_try_set_rating(wnp_player_t* player, unsigned int rating) {
  return _wnp_execute_event(player->id, WNP_TRY_SET_RATING, rating);
}

int wnp_try_set_repeat(wnp_player_t* player, wnp_repeat_t repeat) {
  return _wnp_execute_event(player->id, WNP_TRY_SET_REPEAT, repeat);
}

int wnp_try_set_shuffle(wnp_player_t* player, bool shuffle) {
  return _wnp_execute_event(player->id, WNP_TRY_SET_SHUFFLE, shuffle);
}

/**
 * ===============================
 * | Public util event functions |
 * ===============================
 */

int wnp_try_play_pause(wnp_player_t* player) {
  int is_paused = player->state == WNP_STATE_PAUSED || player->state == WNP_STATE_STOPPED;
  return wnp_try_set_state(player, is_paused ? WNP_STATE_PLAYING : WNP_STATE_PAUSED);
}

int wnp_try_revert(wnp_player_t* player, unsigned int seconds) {
  return wnp_try_set_position(player, player->position - seconds);
}

int wnp_try_forward(wnp_player_t* player, unsigned int seconds) {
  return wnp_try_set_position(player, player->position + seconds);
}

int wnp_try_set_position_percent(wnp_player_t* player, float percent) {
  int seconds = (int)(percent / 100 * player->duration + 0.5);
  return wnp_try_set_position(player, seconds);
}

int wnp_try_revert_percent(wnp_player_t* player, float percent) {
  int seconds = (int)(percent / 100 * player->duration + 0.5);
  return wnp_try_set_position(player, player->position - seconds);
}

int wnp_try_forward_percent(wnp_player_t* player, float percent) {
  int seconds = (int)(percent / 100 * player->duration + 0.5);
  return wnp_try_set_position(player, player->position + seconds);
}

int wnp_try_toggle_repeat(wnp_player_t* player) {
  wnp_repeat_t next_repeat = player->repeat;
  int supports_none = player->available_repeat & WNP_REPEAT_NONE;
  int supports_all = player->available_repeat & WNP_REPEAT_ALL;
  int supports_one = player->available_repeat & WNP_REPEAT_ONE;

  switch (player->repeat) {
    case WNP_REPEAT_NONE:
      next_repeat = supports_all ? WNP_REPEAT_ALL : supports_one ? WNP_REPEAT_ONE : player->repeat;
      break;
    case WNP_REPEAT_ALL:
      next_repeat = supports_one ? WNP_REPEAT_ONE : supports_none ? WNP_REPEAT_NONE : player->repeat;
      break;
    case WNP_REPEAT_ONE:
      next_repeat = supports_none ? WNP_REPEAT_NONE : supports_all ? WNP_REPEAT_ALL : player->repeat;
      break;
  }

  if (next_repeat == player->repeat) {
    return _wnp_failed_event();
  }

  return wnp_try_set_repeat(player, next_repeat);
}
