// c++ because of winrt
#ifdef WNP_BUILD_PLATFORM_WINDOWS

#define THREAD_IMPLEMENTATION

#include "internal.h"
#include "thread.h"
#include "wnp.h"

#include <windows.h>

#include <codecvt>
#include <filesystem>
#include <gdiplus.h>
#include <stdio.h>
#include <string.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Storage.h>

using namespace winrt;
using namespace std::chrono;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Storage::Streams;
using namespace winrt::Windows::Media::Control;
typedef GlobalSystemMediaTransportControlsSessionManager MediaSessionManager;
typedef GlobalSystemMediaTransportControlsSession MediaSession;
typedef GlobalSystemMediaTransportControlsSessionMediaProperties MediaProperties;
typedef GlobalSystemMediaTransportControlsSessionPlaybackInfo PlaybackInfo;
typedef GlobalSystemMediaTransportControlsSessionPlaybackControls PlaybackControls;
typedef GlobalSystemMediaTransportControlsSessionPlaybackStatus PlaybackStatus;
typedef GlobalSystemMediaTransportControlsSessionTimelineProperties TimelineProperties;
typedef winrt::Windows::Media::MediaPlaybackAutoRepeatMode AutoRepeatMode;
typedef winrt::Windows::Storage::Streams::IRandomAccessStreamReference StreamReference;

/**
 * ================================
 * | Definitions and global state |
 * ================================
 */

MediaSessionManager _windows_media_session_manager = NULL;
typedef struct {
  thread_ptr_t thread;
  thread_atomic_int_t thread_exit_flag;
} _windows_state_t;

typedef struct {
  MediaSession session;
  char l_appid[WNP_STR_LEN];
  bool should_remove;
  int position;
  DateTime position_last_updated;
} _windows_platform_data_t;

static _windows_state_t _windows_state;

/**
 * ==============================
 * | Private internal functions |
 * ==============================
 */

static uint64_t _windows_timestamp()
{
  return duration_cast<milliseconds>(utc_clock::now().time_since_epoch()).count();
}

static void _windows_assign_str(char dest[WNP_STR_LEN], const char* str)
{
  if (str == NULL) return;
  size_t len = strlen(str);
  strncpy(dest, str, WNP_STR_LEN - 1);
  dest[len < WNP_STR_LEN ? len : WNP_STR_LEN - 1] = '\0';
}

static void _windows_assign_hstr(char dest[WNP_STR_LEN], hstring src, bool lowercase)
{
  std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
  std::string utf8_str = converter.to_bytes(src.c_str());

  _windows_assign_str(dest, utf8_str.c_str());

  if (lowercase) {
    for (char* p = dest; *p; ++p) {
      *p = tolower(*p);
    }
  }
}

static _windows_platform_data_t* _windows_get_platform_data(wnp_player_t* player)
{
  if (player == NULL || player->platform != WNP_PLATFORM_WINDOWS) {
    return NULL;
  }

  return (_windows_platform_data_t*)player->_platform_data;
}

static bool _windows_is_web_browser(MediaSession session)
{
  char l_appid[WNP_STR_LEN] = {0};
  _windows_assign_hstr(l_appid, session.SourceAppUserModelId(), true);

  if (strstr(l_appid, "chrome") != NULL)
    return true;
  else if (strstr(l_appid, "chromium") != NULL)
    return true;
  else if (strstr(l_appid, "msedge") != NULL)
    return true;
  else if (strstr(l_appid, "opera") != NULL)
    return true;
  else if (strstr(l_appid, "brave") != NULL)
    return true;
  else if (strstr(l_appid, "vivaldi") != NULL)
    return true;
  else if (strstr(l_appid, "firefox") != NULL)
    return true;
  else if (strstr(l_appid, "308046b0af4a39cb") != NULL)
    return true; // firefox 308046B0AF4A39CB
  else if (strstr(l_appid, "6f193ccc56814779") != NULL)
    return true; // firefox nightly 6F193CCC56814779
  else
    return false;
}

static wnp_player_t* _windows_get_player_from_session(wnp_player_t players[WNP_MAX_PLAYERS], int count, MediaSession session)
{
  for (size_t i = 0; i < count; i++) {
    _windows_platform_data_t* platform_data = _windows_get_platform_data(&players[i]);
    if (platform_data != NULL && platform_data->session == session) {
      return &players[i];
    }
  }

  return NULL;
}

// This is used in _windows_on_sessions_changed.
// When players are added or removed, the sessions have new pointers,
// so comparing them by pointer does not work. We compare by appid here
// and also replace _platform_data->session with the new one.
static wnp_player_t* _windows_get_player_from_session_id(wnp_player_t players[WNP_MAX_PLAYERS], int count, MediaSession session)
{
  char l_appid[WNP_STR_LEN] = {0};
  _windows_assign_hstr(l_appid, session.SourceAppUserModelId(), true);

  for (size_t i = 0; i < count; i++) {
    _windows_platform_data_t* platform_data = _windows_get_platform_data(&players[i]);
    if (platform_data != NULL && strcmp(platform_data->l_appid, l_appid) == 0) {
      platform_data->session = session;
      return &players[i];
    }
  }

  return NULL;
}

static void _windows_get_player_name(MediaSession session, char name_out[WNP_STR_LEN])
{
  char l_appid[WNP_STR_LEN] = {0};
  _windows_assign_hstr(l_appid, session.SourceAppUserModelId(), true);

  if (strstr(l_appid, "spotify") != NULL) {
    _windows_assign_str(name_out, "Spotify");
  } else {
    _windows_assign_hstr(name_out, session.SourceAppUserModelId(), false);
    char* exe_position = strstr(name_out, ".exe");
    if (exe_position != NULL) {
      *exe_position = '\0';
    }
  }
}

static bool _windows_is_win10()
{
  static int s_build_number = 0;

  if (s_build_number == 0) {
    HKEY hkey = NULL;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_QUERY_VALUE, &hkey) == ERROR_SUCCESS) {
      wchar_t buffer[10] = {0};
      DWORD size = sizeof(buffer);

      if (RegQueryValueExW(hkey, L"CurrentBuildNumber", NULL, NULL, (LPBYTE)buffer, &size) == ERROR_SUCCESS ||
          RegQueryValueExW(hkey, L"CurrentBuild", NULL, NULL, (LPBYTE)buffer, &size) == ERROR_SUCCESS) {
        s_build_number = _wtoi(buffer);
      }
      RegCloseKey(hkey);
      hkey = NULL;
    }
  }

  return (s_build_number >= 19041 && s_build_number < 22000);
}

static bool _windows_write_thumbnail(int player_id, char l_appid[WNP_STR_LEN], StreamReference stream)
{
  if (stream == NULL) return false;

  char cover_path[WNP_STR_LEN] = {0};
  if (!__wnp_get_cover_path(player_id, cover_path)) {
    return false;
  }

  for (size_t i = 0; cover_path[i] != '\0'; i++) {
    if (cover_path[i] == '/') {
      cover_path[i] = '\\';
    }
  }
  *cover_path += 7;

  std::filesystem::path cover_path_path = std::filesystem::path(cover_path);
  std::filesystem::path folder_path = cover_path_path.parent_path();
  std::filesystem::path file_name = cover_path_path.filename();

  try {
    auto cover_stream = stream.OpenReadAsync().get();
    auto cover_buffer = Buffer(5000000);

    auto folder = StorageFolder::GetFolderFromPathAsync(folder_path.wstring()).get();
    auto cover_file = folder.CreateFileAsync(file_name.wstring(), CreationCollisionOption::ReplaceExisting).get();

    cover_stream.ReadAsync(cover_buffer, cover_buffer.Capacity(), InputStreamOptions::ReadAhead).get();
    FileIO::WriteBufferAsync(cover_file, cover_buffer).get();

    if (_windows_is_win10() && strstr(l_appid, "spotify") != NULL) {
      Gdiplus::GdiplusStartupInput gdiplusStartupInput;
      ULONG_PTR gdiplusToken;
      Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

      {
        hstring cover_path_hstring = winrt::to_hstring(cover_path);
        Gdiplus::Bitmap b = Gdiplus::Bitmap(300, 300);
        Gdiplus::Graphics g = Gdiplus::Graphics(&b);
        Gdiplus::Bitmap cover = Gdiplus::Bitmap(cover_path_hstring.c_str());
        Gdiplus::Rect r = Gdiplus::Rect(0, 0, 300, 300);
        g.DrawImage(&cover, r, 33, 0, 233, 233, Gdiplus::UnitPixel);

        const CLSID pngEncoderClsId = {
            0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}}; // image/png : {557cf406-1a04-11d3-9a73-0000f81ef32e}
        b.Save(cover_path_hstring.c_str(), &pngEncoderClsId, NULL);
      }

      Gdiplus::GdiplusShutdown(gdiplusToken);
    }

    *cover_path -= 7;
    for (size_t i = 0; cover_path[i] != '\0'; i++) {
      if (cover_path[i] == '\\') {
        cover_path[i] = '/';
      }
    }

    return true;
  } catch (const winrt::hresult_error& ex) {
    return false;
  }
}

static void _windows_on_media_properties_changed(wnp_player_t* player, MediaSession session)
{
  try {
    MediaProperties info = session.TryGetMediaPropertiesAsync().get();

    _windows_platform_data_t* platform_data = _windows_get_platform_data(player);
    if (platform_data != NULL) {
      char cover_path[WNP_STR_LEN] = {0};
      if (__wnp_get_cover_path(player->id, cover_path)) {
        if (_windows_write_thumbnail(player->id, platform_data->l_appid, info.Thumbnail())) {
          _windows_assign_str(player->cover, cover_path);
          _windows_assign_str(player->cover_src, cover_path);
        } else {
          _windows_assign_str(player->cover, "");
          _windows_assign_str(player->cover_src, "");
        }
      }
    }

    _windows_assign_hstr(player->artist, info.Artist(), false);
    _windows_assign_hstr(player->album, info.AlbumTitle(), false);

    char new_title[WNP_STR_LEN] = {0};
    _windows_assign_hstr(new_title, info.Title(), false);
    if (strcmp(player->title, new_title) != 0) {
      _windows_assign_str(player->title, new_title);
      if (strlen(new_title) == 0) {
        player->active_at = 0;
      } else {
        player->active_at = _windows_timestamp();
      }
    }

    player->updated_at = _windows_timestamp();
  } catch (...) {
  }
}

static void _windows_on_playback_info_changed(wnp_player_t* player, MediaSession session)
{
  PlaybackInfo info = session.GetPlaybackInfo();
  PlaybackControls controls = info.Controls();
  PlaybackStatus status = info.PlaybackStatus();
  AutoRepeatMode auto_repeat_mode = AutoRepeatMode::None;
  auto arm = info.AutoRepeatMode();
  if (arm) {
    auto_repeat_mode = arm.Value();
  }

  wnp_state_t new_state = status == PlaybackStatus::Playing  ? WNP_STATE_PLAYING
                          : status == PlaybackStatus::Paused ? WNP_STATE_PAUSED
                                                             : WNP_STATE_STOPPED;

  if (player->state != new_state) {
    player->state = new_state;
    player->active_at = _windows_timestamp();
  }

  player->shuffle = info.IsShuffleActive() ? 1 : 0;
  player->repeat = auto_repeat_mode == AutoRepeatMode::List    ? WNP_REPEAT_ALL
                   : auto_repeat_mode == AutoRepeatMode::Track ? WNP_REPEAT_ONE
                                                               : WNP_REPEAT_NONE;

  player->can_set_state = controls.IsPlayPauseToggleEnabled() ? true : false;
  player->can_skip_previous = controls.IsPreviousEnabled() ? true : false;
  player->can_skip_next = controls.IsNextEnabled() ? true : false;
  player->can_set_position = controls.IsPlaybackPositionEnabled() ? true : false;
  player->can_set_volume = false;
  player->can_set_rating = false;
  player->can_set_repeat = controls.IsRepeatEnabled() ? true : false;
  player->can_set_shuffle = controls.IsShuffleEnabled() ? true : false;
  player->updated_at = _windows_timestamp();
}

static void _windows_on_timeline_properties_changed(wnp_player_t* player, MediaSession session)
{
  TimelineProperties info = session.GetTimelineProperties();

  player->duration = duration_cast<seconds>(info.EndTime()).count();
  int position = duration_cast<seconds>(info.Position()).count();
  player->position = position;

  _windows_platform_data_t* platform_data = _windows_get_platform_data(player);
  if (platform_data != NULL) {
    platform_data->position = position;
    platform_data->position_last_updated = info.LastUpdatedTime();
  }

  player->updated_at = _windows_timestamp();
}

static void _windows_on_sessions_changed(MediaSessionManager* manager)
{
  wnp_player_t players[WNP_MAX_PLAYERS] = {0};
  int count = __wnp_start_update_cycle(players);
  for (size_t i = 0; i < count; i++) {
    _windows_platform_data_t* platform_data = _windows_get_platform_data(&players[i]);
    if (platform_data != NULL) {
      platform_data->should_remove = true;
    }
  }

  auto sessions = manager->GetSessions();
  if (!sessions) {
    __wnp_end_update_cycle();
    return;
  }
  for (size_t i = 0; i < sessions.Size(); i++) {
    MediaSession session = sessions.GetAt(i);
    if (!session) continue;
    char l_appid[WNP_STR_LEN] = {0};
    _windows_assign_hstr(l_appid, session.SourceAppUserModelId(), true);

    wnp_player_t* existing_player = _windows_get_player_from_session_id(players, count, session);
    if (existing_player != NULL) {
      _windows_platform_data_t* platform_data = _windows_get_platform_data(existing_player);
      if (platform_data != NULL) {
        platform_data->should_remove = false;
      }
      continue;
    }

    _windows_platform_data_t* platform_data = (_windows_platform_data_t*)calloc(1, sizeof(_windows_platform_data_t));
    if (platform_data == NULL) continue;

    _windows_assign_str(platform_data->l_appid, l_appid);
    platform_data->session = session;
    platform_data->should_remove = false;
    platform_data->position = 0;
    platform_data->position_last_updated = DateTime::clock::now();

    wnp_player_t player = WNP_DEFAULT_PLAYER;
    player._platform_data = platform_data;
    player.platform = WNP_PLATFORM_WINDOWS;
    player.is_web_browser = _windows_is_web_browser(session);
    _windows_get_player_name(session, player.name);
    player.rating_system = WNP_RATING_SYSTEM_NONE;
    player.available_repeat = WNP_REPEAT_NONE | WNP_REPEAT_ALL | WNP_REPEAT_ONE;
    player.created_at = _windows_timestamp();

    player.id = __wnp_add_player(&player);
    if (player.id == -1) {
      free(platform_data);
      continue;
    }
  }

  for (size_t i = 0; i < count; i++) {
    _windows_platform_data_t* platform_data = _windows_get_platform_data(&players[i]);
    if (platform_data != NULL) {
      if (platform_data->should_remove) {
        __wnp_remove_player(players[i].id);
      } else {
        _windows_on_media_properties_changed(&players[i], platform_data->session);
        _windows_on_playback_info_changed(&players[i], platform_data->session);
        _windows_on_timeline_properties_changed(&players[i], platform_data->session);
        __wnp_update_player(&players[i]);
      }
    }
  }

  __wnp_end_update_cycle();
}

static int _windows_thread_func(void* user_data)
{
  thread_timer_t timer;
  thread_timer_init(&timer);
  size_t i = 0;
  thread_atomic_int_t* exit_flag = (thread_atomic_int_t*)user_data;
  while (thread_atomic_int_load(exit_flag) == 0) {
    i++;
    if (i % 2 == 0) {
      _windows_on_sessions_changed(&_windows_media_session_manager);
    }
    wnp_player_t players[WNP_MAX_PLAYERS] = {0};
    int count = __wnp_start_update_cycle(players);
    for (size_t i = 0; i < count; i++) {
      _windows_platform_data_t* platform_data = _windows_get_platform_data(&players[i]);
      if (platform_data != NULL && players[i].state == WNP_STATE_PLAYING && players[i].duration != 0) {
        auto last_updated = platform_data->position_last_updated.time_since_epoch();
        auto now = DateTime::clock::now().time_since_epoch();
        uint64_t start_time = duration_cast<seconds>(last_updated).count();
        uint64_t end_time = duration_cast<seconds>(now).count();
        int delay = (int)(end_time - start_time);
        players[i].position = platform_data->position + delay;
        __wnp_update_player(&players[i]);
      }
    }
    __wnp_end_update_cycle();
    thread_timer_wait(&timer, 500 * 1000000); // 500ms
  }

  thread_timer_term(&timer);
  return 0;
}

/**
 * =============================
 * | Shared internal functions |
 * =============================
 */

extern "C" wnp_init_ret_t __wnp_platform_windows_init()
{
  _windows_media_session_manager = MediaSessionManager::RequestAsync().get();
  thread_atomic_int_store(&_windows_state.thread_exit_flag, 0);
  _windows_state.thread = thread_create(_windows_thread_func, &_windows_state.thread_exit_flag, THREAD_STACK_SIZE_DEFAULT);
  return WNP_INIT_SUCCESS;
}

extern "C" void __wnp_platform_windows_uninit()
{
  thread_atomic_int_store(&_windows_state.thread_exit_flag, 1);
  thread_join(_windows_state.thread);
  thread_destroy(_windows_state.thread);
  _windows_media_session_manager = NULL;
}

extern "C" void __wnp_platform_windows_free(void* _platform_data)
{
  _windows_platform_data_t* platform_data = (_windows_platform_data_t*)_platform_data;

  if (platform_data) {
    free(platform_data);
  }
}

extern "C" void __wnp_platform_windows_event(wnp_player_t* player, wnp_event_t event, int event_id, int data)
{
  _windows_platform_data_t* platform_data = _windows_get_platform_data(player);
  if (platform_data == NULL) return;

  bool success = false;

  switch (event) {
    case WNP_TRY_SET_STATE:
      switch ((wnp_state_t)data) {
        case WNP_STATE_PLAYING:
          success = platform_data->session.TryPlayAsync().get();
          break;
        case WNP_STATE_PAUSED:
          success = platform_data->session.TryPauseAsync().get();
          break;
        case WNP_STATE_STOPPED:
          success = platform_data->session.TryStopAsync().get();
          break;
      }
    case WNP_TRY_SKIP_PREVIOUS:
      success = platform_data->session.TrySkipPreviousAsync().get();
      break;
    case WNP_TRY_SKIP_NEXT:
      success = platform_data->session.TrySkipNextAsync().get();
      break;
    case WNP_TRY_SET_POSITION:
      success = platform_data->session.TryChangePlaybackPositionAsync((int64_t)data * 10000000).get();
      break;
    case WNP_TRY_SET_VOLUME:
      break;
    case WNP_TRY_SET_RATING:
      break;
    case WNP_TRY_SET_REPEAT: {
      AutoRepeatMode auto_repeat_mode = AutoRepeatMode::None;
      switch ((wnp_repeat_t)data) {
        case WNP_REPEAT_NONE:
          auto_repeat_mode = AutoRepeatMode::None;
          break;
        case WNP_REPEAT_ALL:
          auto_repeat_mode = AutoRepeatMode::List;
          break;
        case WNP_REPEAT_ONE:
          auto_repeat_mode = AutoRepeatMode::Track;
          break;
      }
      success = platform_data->session.TryChangeAutoRepeatModeAsync(auto_repeat_mode).get();
      break;
    }
    case WNP_TRY_SET_SHUFFLE:
      success = platform_data->session.TryChangeShuffleActiveAsync(data).get();
      break;
  }

  if (success) {
    __wnp_set_event_result(event_id, WNP_EVENT_SUCCEEDED);
    switch (event) {
      case WNP_TRY_SET_STATE:
        player->state = (wnp_state_t)data;
        break;
      case WNP_TRY_SKIP_PREVIOUS:
      case WNP_TRY_SKIP_NEXT:
        break;
      case WNP_TRY_SET_POSITION:
        player->position = data;
        break;
      case WNP_TRY_SET_VOLUME:
        player->volume = data;
        break;
      case WNP_TRY_SET_RATING:
        player->rating = data;
        break;
      case WNP_TRY_SET_REPEAT:
        player->repeat = (wnp_repeat_t)data;
        break;
      case WNP_TRY_SET_SHUFFLE:
        player->shuffle = data;
        break;
    }
  } else {
    __wnp_set_event_result(event_id, WNP_EVENT_FAILED);
  }
}

#endif /* WNP_BUILD_PLATFORM_WINDOWS */
