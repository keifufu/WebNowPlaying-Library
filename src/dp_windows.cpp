// Only reason this is c++ is because of winrt
// I hate this file as much as I hate windows
#include <windows.h>

#define THREAD_IMPLEMENTATION
#include "thread.h"

#include "wnp.h"
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

using namespace winrt::Windows::Media::Control;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Storage::Streams;
typedef GlobalSystemMediaTransportControlsSessionManager MediaSessionManager;
typedef GlobalSystemMediaTransportControlsSession MediaSession;
typedef GlobalSystemMediaTransportControlsSessionMediaProperties MediaProperties;
typedef GlobalSystemMediaTransportControlsSessionPlaybackInfo PlaybackInfo;
typedef GlobalSystemMediaTransportControlsSessionPlaybackControls PlaybackControls;
typedef GlobalSystemMediaTransportControlsSessionPlaybackStatus PlaybackStatus;
typedef GlobalSystemMediaTransportControlsSessionTimelineProperties TimelineProperties;
typedef winrt::Windows::Media::MediaPlaybackAutoRepeatMode AutoRepeatMode;
typedef winrt::Windows::Storage::Streams::IRandomAccessStreamReference StreamReference;

/* Globals */

bool g_wnp_dp_is_started = false;
MediaSessionManager g_wnp_dp_media_session_manager = NULL;

/* Variables and functions from wnp.c */

extern "C" thread_mutex_t g_wnp_players_mutex;
extern "C" struct wnp_player g_wnp_players[WNP_MAX_PLAYERS];
extern "C" thread_mutex_t g_wnp_event_results_mutex;
extern "C" struct wnp_events g_wnp_events;
extern "C" struct wnp_player* wnp_create_player();
extern "C" void wnp_free_player(struct wnp_player* player);
extern "C" void wnp_recalculate_active_player();
extern "C" int wnp_get_cover_path(int id, char out[WNP_STR_LEN]);
extern "C" void wnp_assign_str(char dest[WNP_STR_LEN], char* str);
extern "C" void wnp_set_event_result(int event_id, int result);

struct wnp_player_data {
  thread_mutex_t lock;
  struct conn_data* conn_data;
  void* dp_data;
};

struct wnp_dp_data {
  char l_appid[WNP_STR_LEN];
  bool should_remove;
  int position;
  winrt::Windows::Foundation::DateTime position_last_updated;
  MediaSession session;
};

int wnp_dp_is_appid_blocked(char* l_appid)
{
  // Finding the AppID of apps is pretty simple, just run
  // `Get-StartApps | Where { $_.Name -eq "Firefox" }`

  // I don't yet understand when an appid is the name of the executable
  // and when its a fucked up msft moment.
  // It doesn't have anything to do with msstore apps, as my normally
  // installed firefox has the fucked id on win11, but the exe name on win10.
  if (strstr(l_appid, "chrome") != NULL)
    return 1;
  else if (strstr(l_appid, "chromium") != NULL)
    return 1;
  else if (strstr(l_appid, "msedge") != NULL)
    return 1;
  else if (strstr(l_appid, "opera") != NULL)
    return 1;
  else if (strstr(l_appid, "brave") != NULL)
    return 1;
  else if (strstr(l_appid, "vivaldi") != NULL)
    return 1;
  else if (strstr(l_appid, "firefox") != NULL)
    return 1;
  else if (strstr(l_appid, "308046b0af4a39cb") != NULL)
    return 1; // firefox 308046B0AF4A39CB
  else if (strstr(l_appid, "6f193ccc56814779") != NULL)
    return 1; // firefox nightly 6F193CCC56814779
  else
    return 0;
}

void wnp_dp_own_hstring(winrt::hstring hstring, int to_lower, char str[WNP_STR_LEN])
{
  std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
  std::string utf8_str = converter.to_bytes(hstring.c_str());

  strcpy(str, (char*)utf8_str.c_str());

  if (to_lower) {
    for (char* p = str; *p; ++p)
      *p = tolower(*p);
  }

  int len = strlen((char*)utf8_str.c_str());
  str[len < WNP_STR_LEN ? len : WNP_STR_LEN - 1] = '\0';
}

struct wnp_dp_data* wnp_get_dp_data(struct wnp_player* player)
{
  if (player == NULL || player->id == -1 || !player->is_desktop_player) {
    return NULL;
  }

  struct wnp_player_data* player_data = (struct wnp_player_data*)player->_data;
  struct wnp_dp_data* dp_data = (struct wnp_dp_data*)player_data->dp_data;
  return dp_data;
}

struct wnp_player* wnp_dp_get_player_from_session(MediaSession session)
{
  thread_mutex_lock(&g_wnp_players_mutex);
  for (int i = 0; i < WNP_MAX_PLAYERS; i++) {
    struct wnp_dp_data* dp_data = wnp_get_dp_data(&g_wnp_players[i]);
    if (dp_data != NULL && dp_data->session == session) {
      struct wnp_player* player = &g_wnp_players[i];
      thread_mutex_unlock(&g_wnp_players_mutex);
      return player;
    }
  }
  thread_mutex_unlock(&g_wnp_players_mutex);
  return NULL;
}

long long wnp_dp_get_timestamp()
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::utc_clock::now().time_since_epoch()).count();
}

void wnp_dp_get_player_name(MediaSession session, char name[WNP_STR_LEN])
{
  char l_appid[WNP_STR_LEN];
  wnp_dp_own_hstring(session.SourceAppUserModelId(), 1, l_appid);

  if (strstr(l_appid, "spotify") != NULL) {
    wnp_assign_str(name, (char*)"Spotify Desktop");
  } else {
    wnp_dp_own_hstring(session.SourceAppUserModelId(), 0, name);
  }
}

int wnp_dp_is_windows_10()
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

char* wnp_dp_write_thumbnail(StreamReference stream, char l_appid[WNP_STR_LEN], int player_id)
{
  if (stream == NULL) return NULL;

  char _cover_path[WNP_STR_LEN];
  int ret = wnp_get_cover_path(player_id, _cover_path);
  if (ret != 0) return NULL;
  char* cover_path = strdup(_cover_path);
  for (int i = 0; cover_path[i] != '\0'; i++) {
    if (cover_path[i] == '/') {
      cover_path[i] = '\\';
    }
  }
  cover_path += 7;

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

    if (wnp_dp_is_windows_10() && strstr(l_appid, "spotify") != NULL) {
      Gdiplus::GdiplusStartupInput gdiplusStartupInput;
      ULONG_PTR gdiplusToken;
      Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

      {
        winrt::hstring cover_path_hstring = winrt::to_hstring(cover_path);
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

    cover_path -= 7;
    for (int i = 0; cover_path[i] != '\0'; i++) {
      if (cover_path[i] == '\\') {
        cover_path[i] = '/';
      }
    }
    return cover_path;
  } catch (const winrt::hresult_error& ex) {
    return NULL;
  }
}

void wnp_dp_on_media_properties_changed(MediaSession session)
{
  struct wnp_player* player = wnp_dp_get_player_from_session(session);
  if (player == NULL) return;

  try {
    MediaProperties info = session.TryGetMediaPropertiesAsync().get();
    wnp_lock(player);

    struct wnp_dp_data* dp_data = wnp_get_dp_data(player);
    if (dp_data != NULL) {
      char* cover_src = wnp_dp_write_thumbnail(info.Thumbnail(), dp_data->l_appid, player->id);
      if (cover_src != NULL) {
        wnp_assign_str(player->cover_src, cover_src);
        wnp_assign_str(player->cover, cover_src);
        free(cover_src);
      } else {
        wnp_assign_str(player->cover_src, (char*)"");
        wnp_assign_str(player->cover, (char*)"");
      }
    }

    char artist[WNP_STR_LEN];
    wnp_dp_own_hstring(info.Artist(), 0, artist);
    wnp_assign_str(player->artist, artist);

    char album[WNP_STR_LEN];
    wnp_dp_own_hstring(info.AlbumTitle(), 0, album);
    wnp_assign_str(player->album, album);

    char new_title[WNP_STR_LEN];
    wnp_dp_own_hstring(info.Title(), 0, new_title);
    if (strcmp(player->title, new_title) != 0) {
      wnp_assign_str(player->title, new_title);
      if (strlen(new_title) == 0) {
        player->active_at = 0;
      } else {
        player->active_at = wnp_dp_get_timestamp();
      }
    }

    player->updated_at = wnp_dp_get_timestamp();
    wnp_unlock(player);

    if (g_wnp_events.on_player_updated != NULL) {
      g_wnp_events.on_player_updated(player);
    }
    wnp_recalculate_active_player();
  } catch (...) {
  }
}

void wnp_dp_on_playback_info_changed(MediaSession session)
{
  struct wnp_player* player = wnp_dp_get_player_from_session(session);
  if (player == NULL) return;

  PlaybackInfo info = session.GetPlaybackInfo();
  PlaybackControls controls = info.Controls();
  PlaybackStatus status = info.PlaybackStatus();
  AutoRepeatMode auto_repeat_mode = AutoRepeatMode::None;
  auto arm = info.AutoRepeatMode();
  if (arm) {
    auto_repeat_mode = arm.Value();
  }

  enum wnp_state new_state = status == PlaybackStatus::Playing  ? WNP_STATE_PLAYING
                             : status == PlaybackStatus::Paused ? WNP_STATE_PAUSED
                                                                : WNP_STATE_STOPPED;

  wnp_lock(player);

  if (player->state != new_state) {
    player->state = new_state;
    player->active_at = wnp_dp_get_timestamp();
  }

  player->shuffle = info.IsShuffleActive() ? 1 : 0;
  player->repeat = auto_repeat_mode == AutoRepeatMode::List    ? WNP_REPEAT_ALL
                   : auto_repeat_mode == AutoRepeatMode::Track ? WNP_REPEAT_ONE
                                                               : WNP_REPEAT_NONE;
  player->can_set_state = controls.IsPlayPauseToggleEnabled() ? 1 : 0;
  player->can_skip_previous = controls.IsPreviousEnabled() ? 1 : 0;
  player->can_skip_next = controls.IsNextEnabled() ? 1 : 0;
  player->can_set_position = controls.IsPlaybackPositionEnabled() ? 1 : 0;
  player->can_set_volume = 0;
  player->can_set_rating = 0;
  player->can_set_repeat = controls.IsRepeatEnabled() ? 1 : 0;
  player->can_set_shuffle = controls.IsShuffleEnabled() ? 1 : 0;
  player->updated_at = wnp_dp_get_timestamp();

  wnp_unlock(player);

  if (g_wnp_events.on_player_updated != NULL) {
    g_wnp_events.on_player_updated(player);
  }
  wnp_recalculate_active_player();
}

void wnp_dp_on_timeline_properties_changed(MediaSession session)
{
  struct wnp_player* player = wnp_dp_get_player_from_session(session);
  if (player == NULL) return;

  TimelineProperties info = session.GetTimelineProperties();

  wnp_lock(player);

  player->duration = std::chrono::duration_cast<std::chrono::seconds>(info.EndTime()).count();
  int position = std::chrono::duration_cast<std::chrono::seconds>(info.Position()).count();
  player->position = position;
  struct wnp_dp_data* dp_data = wnp_get_dp_data(player);
  if (dp_data != NULL) {
    dp_data->position = position;
    dp_data->position_last_updated = info.LastUpdatedTime();
  }

  player->updated_at = wnp_dp_get_timestamp();

  wnp_unlock(player);

  if (g_wnp_events.on_player_updated != NULL) {
    g_wnp_events.on_player_updated(player);
  }
  wnp_recalculate_active_player();
}

void wnp_dp_on_sessions_changed(MediaSessionManager manager)
{
  // Mark all desktop players as should_remove
  thread_mutex_lock(&g_wnp_players_mutex);
  for (int i = 0; i < WNP_MAX_PLAYERS; i++) {
    struct wnp_dp_data* dp_data = wnp_get_dp_data(&g_wnp_players[i]);
    if (dp_data != NULL) {
      dp_data->should_remove = true;
    }
  }
  thread_mutex_unlock(&g_wnp_players_mutex);

  auto sessions = manager.GetSessions();
  if (!sessions) return;
  for (int i = 0; i < sessions.Size(); i++) {
    MediaSession session = sessions.GetAt(i);
    if (!session) continue;
    char l_appid[WNP_STR_LEN];
    wnp_dp_own_hstring(session.SourceAppUserModelId(), 1, l_appid);

    if (!wnp_dp_is_appid_blocked(l_appid)) {
      // If we already track the player, unmark should_remove and continue.
      struct wnp_player* existing_player = wnp_dp_get_player_from_session(session);
      if (existing_player != NULL) {
        struct wnp_dp_data* dp_data = wnp_get_dp_data(existing_player);
        if (dp_data != NULL) {
          dp_data->should_remove = false;
        }
        continue;
      }

      struct wnp_player* player = wnp_create_player();
      if (player == NULL) continue;
      wnp_lock(player);

      struct wnp_dp_data* dp_data = (struct wnp_dp_data*)calloc(1, sizeof(struct wnp_dp_data));
      if (dp_data == NULL) {
        wnp_unlock(player);
        wnp_free_player(player);
        continue;
      }

      wnp_assign_str(dp_data->l_appid, l_appid);
      dp_data->session = session;
      dp_data->should_remove = false;

      struct wnp_player_data* player_data = (struct wnp_player_data*)player->_data;
      player_data->dp_data = dp_data;

      char name[WNP_STR_LEN];
      wnp_dp_get_player_name(session, name);
      wnp_assign_str(player->name, name);
      player->created_at = wnp_dp_get_timestamp();
      player->is_desktop_player = 1;
      player->rating_system = WNP_RATING_SYSTEM_NONE;
      player->available_repeat = WNP_REPEAT_NONE | WNP_REPEAT_ALL | WNP_REPEAT_ONE;
      player->volume = 100;
      wnp_unlock(player);

      if (g_wnp_events.on_player_added != NULL) {
        g_wnp_events.on_player_added(&g_wnp_players[i]);
      }
    }
  }

  // Remove all players that are still marked as should_remove
  thread_mutex_lock(&g_wnp_players_mutex);
  for (int i = 0; i < WNP_MAX_PLAYERS; i++) {
    struct wnp_dp_data* dp_data = wnp_get_dp_data(&g_wnp_players[i]);
    if (dp_data != NULL) {
      if (dp_data->should_remove) {
        if (g_wnp_events.on_player_removed != NULL) {
          g_wnp_events.on_player_removed(&g_wnp_players[i]);
        }
        thread_mutex_unlock(&g_wnp_players_mutex);
        wnp_free_player(&g_wnp_players[i]);
        thread_mutex_lock(&g_wnp_players_mutex);
      } else {
        wnp_dp_on_media_properties_changed(dp_data->session);
        wnp_dp_on_playback_info_changed(dp_data->session);
        wnp_dp_on_timeline_properties_changed(dp_data->session);
      }
    }
  }
  thread_mutex_unlock(&g_wnp_players_mutex);
  wnp_recalculate_active_player();
}

bool wnp_dp_stop_thread = false;
std::thread wnp_dp_thread;

// msft callbacks no worke, same with old c# wnp and seemingly a lot of other projects, fun times.
// stupid ass windows already spends 10% cpu fucking idling istg, a bit more idle usage wont make a difference.
void wnp_dp_update_thread()
{
  int i = 0;
  while (!wnp_dp_stop_thread) {
    i++;
    if (i % 2 == 0) {
      wnp_dp_on_sessions_changed(g_wnp_dp_media_session_manager);
    }
    thread_mutex_lock(&g_wnp_players_mutex);
    for (int i = 0; i < WNP_MAX_PLAYERS; i++) {
      if (g_wnp_players[i].id != -1 && g_wnp_players[i].is_desktop_player && g_wnp_players[i].state == WNP_STATE_PLAYING &&
          g_wnp_players[i].duration != 0) {
        wnp_lock(&g_wnp_players[i]);
        struct wnp_dp_data* dp_data = wnp_get_dp_data(&g_wnp_players[i]);
        auto last_updated = dp_data->position_last_updated.time_since_epoch();
        auto now = winrt::Windows::Foundation::DateTime::clock::now().time_since_epoch();
        long long start_time = std::chrono::duration_cast<std::chrono::seconds>(last_updated).count();
        long long end_time = std::chrono::duration_cast<std::chrono::seconds>(now).count();
        int delay = (int)(end_time - start_time);
        g_wnp_players[i].position = dp_data->position + delay;
        wnp_unlock(&g_wnp_players[i]);
      }
    }
    thread_mutex_unlock(&g_wnp_players_mutex);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
}

extern "C" void wnp_dp_start()
{
  if (g_wnp_dp_is_started) return;
  g_wnp_dp_is_started = true;
  winrt::init_apartment();
  g_wnp_dp_media_session_manager = MediaSessionManager::RequestAsync().get();
  wnp_dp_stop_thread = false;
  wnp_dp_thread = std::thread(wnp_dp_update_thread);
}

extern "C" void wnp_dp_stop()
{
  if (!g_wnp_dp_is_started) return;
  g_wnp_dp_is_started = false;
  wnp_dp_stop_thread = true;
  if (wnp_dp_thread.joinable()) {
    wnp_dp_thread.join();
  }

  thread_mutex_lock(&g_wnp_players_mutex);
  for (int i = 0; i < WNP_MAX_PLAYERS; i++) {
    if (g_wnp_players[i].id != -1 && g_wnp_players[i].is_desktop_player) {
      if (g_wnp_events.on_player_removed != NULL) {
        g_wnp_events.on_player_removed(&g_wnp_players[i]);
      }
      thread_mutex_unlock(&g_wnp_players_mutex);
      wnp_free_player(&g_wnp_players[i]);
      thread_mutex_lock(&g_wnp_players_mutex);
    }
  }
  thread_mutex_unlock(&g_wnp_players_mutex);
  winrt::uninit_apartment();
}

extern "C" void wnp_dp_free_dp_data(struct wnp_dp_data* dp_data)
{
  free(dp_data);
}

extern "C" void wnp_dp_try_set_state(struct wnp_player* player, int event_id, enum wnp_state state)
{
  struct wnp_dp_data* dp_data = wnp_get_dp_data(player);
  if (dp_data == NULL) {
    wnp_set_event_result(event_id, WNP_EVENT_FAILED);
    return;
  }

  wnp_lock(player);
  enum wnp_state old_state = player->state;
  player->state = state;
  wnp_unlock(player);

  switch (state) {
  case WNP_STATE_PLAYING:
    if (dp_data->session.TryPlayAsync().get()) {
      wnp_set_event_result(event_id, WNP_EVENT_SUCCEEDED);
    } else {
      wnp_set_event_result(event_id, WNP_EVENT_FAILED);
      wnp_lock(player);
      player->state = old_state;
      wnp_unlock(player);
    }
    break;
  case WNP_STATE_PAUSED:
    if (dp_data->session.TryPauseAsync().get()) {
      wnp_set_event_result(event_id, WNP_EVENT_SUCCEEDED);
    } else {
      wnp_set_event_result(event_id, WNP_EVENT_FAILED);
      wnp_lock(player);
      player->state = old_state;
      wnp_unlock(player);
    }
    break;
  case WNP_STATE_STOPPED:
    if (dp_data->session.TryStopAsync().get()) {
      wnp_set_event_result(event_id, WNP_EVENT_SUCCEEDED);
    } else {
      wnp_set_event_result(event_id, WNP_EVENT_FAILED);
      wnp_lock(player);
      player->state = old_state;
      wnp_unlock(player);
    }
    break;
  }
}

extern "C" void wnp_dp_try_skip_previous(struct wnp_player* player, int event_id)
{
  struct wnp_dp_data* dp_data = wnp_get_dp_data(player);
  if (dp_data == NULL) {
    wnp_set_event_result(event_id, WNP_EVENT_FAILED);
    return;
  }

  if (dp_data->session.TrySkipPreviousAsync().get()) {
    wnp_set_event_result(event_id, WNP_EVENT_SUCCEEDED);
  } else {
    wnp_set_event_result(event_id, WNP_EVENT_FAILED);
  }
}

extern "C" void wnp_dp_try_skip_next(struct wnp_player* player, int event_id)
{
  struct wnp_dp_data* dp_data = wnp_get_dp_data(player);
  if (dp_data == NULL) {
    wnp_set_event_result(event_id, WNP_EVENT_FAILED);
    return;
  }

  if (dp_data->session.TrySkipNextAsync().get()) {
    wnp_set_event_result(event_id, WNP_EVENT_SUCCEEDED);
  } else {
    wnp_set_event_result(event_id, WNP_EVENT_FAILED);
  }
}

extern "C" void wnp_dp_try_set_position(struct wnp_player* player, int event_id, int seconds)
{
  struct wnp_dp_data* dp_data = wnp_get_dp_data(player);
  if (dp_data == NULL) {
    wnp_set_event_result(event_id, WNP_EVENT_FAILED);
    return;
  }

  wnp_lock(player);
  int old_position = player->position;
  player->position = seconds;
  wnp_unlock(player);

  int64_t position = seconds * 10000000;
  if (dp_data->session.TryChangePlaybackPositionAsync(position).get()) {
    wnp_set_event_result(event_id, WNP_EVENT_SUCCEEDED);
  } else {
    wnp_set_event_result(event_id, WNP_EVENT_FAILED);
    wnp_lock(player);
    player->position = old_position;
    wnp_unlock(player);
  }
}

extern "C" void wnp_dp_try_set_volume(struct wnp_player* player, int event_id, int volume)
{
  wnp_set_event_result(event_id, WNP_EVENT_FAILED);
}

extern "C" void wnp_dp_try_set_rating(struct wnp_player* player, int event_id, int rating)
{
  wnp_set_event_result(event_id, WNP_EVENT_FAILED);
}

extern "C" void wnp_dp_try_set_repeat(struct wnp_player* player, int event_id, enum wnp_repeat repeat)
{
  struct wnp_dp_data* dp_data = wnp_get_dp_data(player);
  if (dp_data == NULL) {
    wnp_set_event_result(event_id, WNP_EVENT_FAILED);
    return;
  }

  wnp_lock(player);
  enum wnp_repeat old_repeat = player->repeat;
  player->repeat = repeat;
  wnp_unlock(player);

  AutoRepeatMode auto_repeat_mode = AutoRepeatMode::None;
  switch (repeat) {
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

  if (dp_data->session.TryChangeAutoRepeatModeAsync(auto_repeat_mode).get()) {
    wnp_set_event_result(event_id, WNP_EVENT_SUCCEEDED);
  } else {
    wnp_set_event_result(event_id, WNP_EVENT_FAILED);
    wnp_lock(player);
    player->repeat = old_repeat;
    wnp_unlock(player);
  }
}

extern "C" void wnp_dp_try_set_shuffle(struct wnp_player* player, int event_id, bool shuffle)
{
  struct wnp_dp_data* dp_data = wnp_get_dp_data(player);
  if (dp_data == NULL) {
    wnp_set_event_result(event_id, WNP_EVENT_FAILED);
    return;
  }

  wnp_lock(player);
  int old_shuffle = player->shuffle;
  player->shuffle = shuffle;
  wnp_unlock(player);

  if (dp_data->session.TryChangeShuffleActiveAsync(shuffle).get()) {
    wnp_set_event_result(event_id, WNP_EVENT_SUCCEEDED);
  } else {
    wnp_set_event_result(event_id, WNP_EVENT_FAILED);
    wnp_lock(player);
    player->shuffle = old_shuffle;
    wnp_unlock(player);
  }
}
