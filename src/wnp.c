#include "wnp.h"
#include "cws.h"
#include "thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define F_OK 0
#define access _access
#include <windows.h>
#else
#include <unistd.h>
#endif

/**
 * Using a stupid prefix instead of static so I can
 * share everything across dp_* files and tests/_.c
 */

/**
 * ===========================
 * | Structs and global vars |
 * ===========================
 *
 * Global variables are initialized
 * in `wnp_init_globals`.
 */

enum wnp_message_type {
  PLAYER_ADDED = 0,
  PLAYER_UPDATED = 1,
  PLAYER_REMOVED = 2,
  EVENT_RESULT = 3,
  USE_DESKTOP_PLAYERS = 4,
};

enum wnp_event {
  TRY_SET_STATE = 0,
  TRY_SKIP_PREVIOUS = 1,
  TRY_SKIP_NEXT = 2,
  TRY_SET_POSITION = 3,
  TRY_SET_VOLUME = 4,
  TRY_SET_RATING = 5,
  TRY_SET_REPEAT = 6,
  TRY_SET_SHUFFLE = 7,
};

struct wnp_player WNP_DEFAULT_PLAYER = {
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
    .is_desktop_player = false,
    ._data = NULL,
};

struct wnp_field_action {
  void* dest;
  int type; // 0 = char[WNP_STR_LEN], 1 = int, 2 = long long
};

struct wnp_player_data {
  thread_mutex_t lock;
  struct wnp_conn_data* conn_data;
  void* dp_data;
};

struct wnp_conn_data {
  cws_client_t* client;
  int port_id;
};

struct wnp_cover_buffer {
  void* data;
  uint64_t data_size;
  cws_client_t* client;
  int port_id;
};

#define WNP_MAX_COVER_BUFFERS WNP_MAX_PLAYERS

thread_mutex_t g_wnp_players_mutex;
struct wnp_player g_wnp_players[WNP_MAX_PLAYERS] = {0};
thread_mutex_t g_wnp_cover_buffers_mutex;
struct wnp_cover_buffer* g_wnp_cover_buffers[WNP_MAX_COVER_BUFFERS] = {0};
thread_mutex_t g_wnp_event_results_mutex;
enum wnp_event_result g_wnp_event_results[WNP_MAX_EVENT_RESULTS] = {0};
char g_wnp_adapter_version[5] = "0.0.0";
struct wnp_events g_wnp_events = {0};
int g_wnp_active_player_id = -1;
bool g_wnp_is_started = false;
int g_wnp_use_dp = 1;

/**
 * ============================
 * | Desktop Player functions |
 * ============================
 */

#ifndef WNP_NODP
extern void wnp_dp_start();
extern void wnp_dp_stop();
extern void wnp_dp_free_dp_data(void* dp_data);
extern void wnp_dp_try_set_state(struct wnp_player* player, int event_id, enum wnp_state state);
extern void wnp_dp_try_skip_previous(struct wnp_player* player, int event_id);
extern void wnp_dp_try_skip_next(struct wnp_player* player, int event_id);
extern void wnp_dp_try_set_position(struct wnp_player* player, int event_id, int seconds);
extern void wnp_dp_try_set_volume(struct wnp_player* player, int event_id, int volume);
extern void wnp_dp_try_set_rating(struct wnp_player* player, int event_id, int rating);
extern void wnp_dp_try_set_repeat(struct wnp_player* player, int event_id, enum wnp_repeat repeat);
extern void wnp_dp_try_set_shuffle(struct wnp_player* player, int event_id, bool shuffle);
#endif

char* wnp_nodp_filepath()
{
  static char path[WNP_STR_LEN];
  static bool path_computed = false;

  if (path_computed) {
    return path;
  }

  char* file_name = ".wnp_nodp";
#ifdef _WIN32
  char* home = getenv("USERPROFILE");
  if (home == NULL || strlen(home) + strlen(file_name) + 2 > WNP_STR_LEN) {
    return NULL;
  }

  strcpy(path, home);
  strcat(path, "\\");
  strcat(path, file_name);
#else
  char* home = getenv("HOME");
  if (home == NULL || strlen(home) + strlen(file_name) + 2 > WNP_STR_LEN) {
    return NULL;
  }

  strcpy(path, home);
  strcat(path, "/");
  strcat(path, file_name);
#endif

  path_computed = true;
  return path;
}

bool wnp_set_use_dp(int use_dp)
{
  char* path = wnp_nodp_filepath();
  if (path == NULL) return false;
  if (use_dp) {
    if (access(path, F_OK) == 0) {
      remove(path);
    }
  } else {
    if (access(path, F_OK) != 0) {
      FILE* fptr = fopen(path, "w");
      fclose(fptr);
    }
  }
  g_wnp_use_dp = use_dp;
  return use_dp;
}

bool wnp_read_use_dp()
{
  char* path = wnp_nodp_filepath();
  if (path == NULL) return 1;
  int use_dp = (access(path, F_OK) != 0);
  return use_dp;
}

/**
 * ===============================
 * | Player management functions |
 * ===============================
 */

void wnp_assign_str(char dest[WNP_STR_LEN], char* str)
{
  size_t len = strlen(str);
  strncpy(dest, str, WNP_STR_LEN - 1);
  dest[len < WNP_STR_LEN ? len : WNP_STR_LEN - 1] = '\0';
}

struct wnp_player* wnp_create_player()
{
  struct wnp_player* player = NULL;
  thread_mutex_lock(&g_wnp_players_mutex);
  for (size_t i = 0; i < WNP_MAX_PLAYERS; i++) {
    if (g_wnp_players[i].id == -1) {
      player = &g_wnp_players[i];
      player->id = i;
      break;
    }
  }
  thread_mutex_unlock(&g_wnp_players_mutex);
  if (player == NULL) return NULL;

  struct wnp_player_data* player_data = (struct wnp_player_data*)calloc(1, sizeof(struct wnp_player_data));
  if (player_data == NULL) {
    return NULL;
  }
  player_data->conn_data = NULL;
  player_data->dp_data = NULL;

  player->_data = player_data;
  wnp_assign_str(player->name, "");
  wnp_assign_str(player->title, "");
  wnp_assign_str(player->artist, "");
  wnp_assign_str(player->album, "");
  wnp_assign_str(player->cover, "");
  wnp_assign_str(player->cover_src, "");
  player->state = WNP_STATE_STOPPED;
  player->position = 0;
  player->duration = 0;
  player->volume = 100;
  player->rating = 0;
  player->repeat = WNP_REPEAT_NONE;
  player->shuffle = false;
  player->rating_system = WNP_RATING_SYSTEM_NONE;
  player->available_repeat = 0;
  player->can_set_state = false;
  player->can_skip_previous = false;
  player->can_skip_next = false;
  player->can_set_position = false;
  player->can_set_volume = false;
  player->can_set_rating = false;
  player->can_set_repeat = false;
  player->can_set_shuffle = false;
  player->created_at = 0;
  player->updated_at = 0;
  player->active_at = 0;
  player->is_desktop_player = false;

  thread_mutex_init(&player_data->lock);

  return player;
}

/**
 * This is called:
 * - at the end of `wnp_free_player`
 * - at the end of `wnp_on_message`
 * - at the end of `wnp_stop`
 *
 * dp_windows.cpp:
 * - at the end of `on_media_properties_changed`
 * - at the end of `on_playback_info_changed`
 * - at the end of `on_timeline_properties_changed`
 * - at the end of `on_sessions_changed`
 */
void wnp_recalculate_active_player()
{
  struct wnp_player* active_player = NULL;
  long long max_active_at = 0;
  bool found_active = false;

  thread_mutex_lock(&g_wnp_players_mutex);
  for (size_t i = 0; i < WNP_MAX_PLAYERS; i++) {
    if (g_wnp_players[i].id != -1) {
      if (g_wnp_players[i].state == WNP_STATE_PLAYING && g_wnp_players[i].volume != 0 && g_wnp_players[i].active_at > max_active_at) {
        active_player = &g_wnp_players[i];
        max_active_at = g_wnp_players[i].active_at;
        found_active = true;
      } else if (g_wnp_players[i].state == WNP_STATE_PLAYING && !found_active) {
        active_player = &g_wnp_players[i];
        max_active_at = g_wnp_players[i].active_at;
      } else if (g_wnp_players[i].state != WNP_STATE_PLAYING && g_wnp_players[i].active_at > max_active_at && !found_active) {
        bool is_active_playing = active_player == NULL ? false : active_player->state == WNP_STATE_PLAYING;
        if (!is_active_playing) {
          active_player = &g_wnp_players[i];
          max_active_at = g_wnp_players[i].active_at;
        }
      }
    }
  }
  thread_mutex_unlock(&g_wnp_players_mutex);

  if (active_player == NULL || active_player->id == -1) {
    g_wnp_active_player_id = -1;
    if (g_wnp_events.on_active_player_changed != NULL) {
      g_wnp_events.on_active_player_changed(NULL, g_wnp_events.data);
    }
  } else {
    if (g_wnp_active_player_id != active_player->id) {
      g_wnp_active_player_id = active_player->id;
      if (g_wnp_events.on_active_player_changed != NULL) {
        g_wnp_events.on_active_player_changed(active_player, g_wnp_events.data);
      }
    }
  }
}

void wnp_free_player(struct wnp_player* player)
{
  if (player != NULL && player->id != -1) {
    wnp_lock(player);
    thread_mutex_lock(&g_wnp_players_mutex);

    struct wnp_player_data* player_data = player->_data;
    if (player_data != NULL) {
      thread_mutex_term(&player_data->lock);

      if (player_data->conn_data != NULL) {
        free(player_data->conn_data);
      }

#ifndef WNP_NODP
      if (player_data->dp_data != NULL) {
        wnp_dp_free_dp_data(player_data->dp_data);
      }
#endif

      free(player_data);
    }

    g_wnp_players[player->id] = WNP_DEFAULT_PLAYER;

    thread_mutex_unlock(&g_wnp_players_mutex);
    wnp_recalculate_active_player();
  }
}

void wnp_parse_player_text(struct wnp_player* player, char* data)
{
  const struct wnp_field_action actions[] = {
      {&(((struct wnp_player_data*)player->_data)->conn_data->port_id), 1},
      {&(player->name), 0},
      {&(player->title), 0},
      {&(player->artist), 0},
      {&(player->album), 0},
      {&(player->cover_src), 0},
      {&(player->state), 1},
      {&(player->position), 1},
      {&(player->duration), 1},
      {&(player->volume), 1},
      {&(player->rating), 1},
      {&(player->repeat), 1},
      {&(player->shuffle), 1},
      {&(player->rating_system), 1},
      {&(player->available_repeat), 1},
      {&(player->can_set_state), 1},
      {&(player->can_skip_previous), 1},
      {&(player->can_skip_next), 1},
      {&(player->can_set_position), 1},
      {&(player->can_set_volume), 1},
      {&(player->can_set_rating), 1},
      {&(player->can_set_repeat), 1},
      {&(player->can_set_shuffle), 1},
      {&(player->created_at), 2},
      {&(player->updated_at), 2},
      {&(player->active_at), 2},
  };

  int field_len = sizeof(actions) / sizeof(actions[0]);
  int field_counter = 0;
  char* p = data;
  char* token_start = p;

  wnp_lock(player);

  while (field_counter < field_len) {
    if (*p == '\\' && *(p + 1) == '|') {
      p++;
    } else if (*p == '\0' || *p == '|') {
      if (*p == '\0') break; // End of data

      *p = '\0'; //  Terminate the token
      char* token = token_start;

      char* src = token_start;
      char* dest = token_start;
      while (*src != '\0') {
        if (*src == '\\' && *(src + 1) == '|') {
          src++; // Skip the escape character
        }
        *dest++ = *src++;
      }
      *dest = '\0'; // Terminate the processed token

      if (*token != '\0') {
        const struct wnp_field_action* action = &actions[field_counter];
        switch (action->type) {
        case 0:
          wnp_assign_str(action->dest, (*token == '\1') ? "" : token);
          break;
        case 1:
          *(int*)action->dest = atoi(token);
          break;
        case 2:
          *(long long*)action->dest = strtoll(token, NULL, 10);
          break;
        }
      }

      token_start = p + 1;
      field_counter++;
    }
    p++;
  }

  wnp_unlock(player);
}

struct wnp_conn_data* wnp_get_conn_data(struct wnp_player* player)
{
  if (player == NULL || player->id == -1 || player->is_desktop_player) {
    return NULL;
  }

  struct wnp_player_data* player_data = player->_data;
  struct wnp_conn_data* conn_data = player_data->conn_data;
  return conn_data;
}

/**
 * ===================
 * | Cover functions |
 * ===================
 */

int wnp_get_cover_path(int id, char out[WNP_STR_LEN])
{
  char file_name[WNP_STR_LEN];
  snprintf(file_name, WNP_STR_LEN, "libwnp-cover-%d.png", id);

#ifdef _WIN32
  char* tmp = getenv("TEMP");
  if (tmp == NULL || strlen(tmp) + strlen(file_name) + 2 > WNP_STR_LEN) {
    return 1;
  }

  for (int i = 0; tmp[i] != '\0'; i++) {
    if (tmp[i] == '\\') {
      tmp[i] = '/';
    }
  }
  snprintf(out, WNP_STR_LEN, "file://%s/%s", tmp, file_name);
#else
  snprintf(out, WNP_STR_LEN, "file:///tmp/%s", file_name);
#endif

  return 0;
}

void wnp_write_cover(int id, void* data, uint64_t size)
{
  char file_path[WNP_STR_LEN];
  int ret = wnp_get_cover_path(id, file_path);
  if (ret != 0) return;

  FILE* file = fopen(file_path + 7, "wb");
  if (file == NULL) {
    return;
  }
  size_t bytes_written = fwrite(data, sizeof(unsigned char), size, file);
  if (bytes_written != size) {
    fclose(file);
    return;
  }
  fclose(file);
}

/**
 * ===================
 * | Event functions |
 * ===================
 */

void wnp_set_event_result(int event_id, int result)
{
  thread_mutex_lock(&g_wnp_event_results_mutex);
  g_wnp_event_results[event_id] = result;
  thread_mutex_unlock(&g_wnp_event_results_mutex);
}

int wnp_get_event_id()
{
  static int id = 0;
  id = (id + 1) % WNP_MAX_EVENT_RESULTS;
  wnp_set_event_result(id, WNP_EVENT_PENDING);
  return id;
}

int wnp_failed_event()
{
  int event_id = wnp_get_event_id();
  wnp_set_event_result(event_id, WNP_EVENT_FAILED);
  return event_id;
}

int wnp_execute_event(struct wnp_player* player, enum wnp_event event, int data)
{
  // Fail event if player doesn't support it currently
  switch (event) {
  case TRY_SET_STATE:
    if (!player->can_set_state) return wnp_failed_event();
    break;
  case TRY_SKIP_PREVIOUS:
    if (!player->can_skip_previous) return wnp_failed_event();
    break;
  case TRY_SKIP_NEXT:
    if (!player->can_skip_next) return wnp_failed_event();
    break;
  case TRY_SET_POSITION:
    if (!player->can_set_position) return wnp_failed_event();
    break;
  case TRY_SET_VOLUME:
    if (!player->can_set_volume) return wnp_failed_event();
    break;
  case TRY_SET_RATING:
    if (!player->can_set_rating) return wnp_failed_event();
    break;
  case TRY_SET_REPEAT:
    if (!player->can_set_repeat) return wnp_failed_event();
    break;
  case TRY_SET_SHUFFLE:
    if (!player->can_set_shuffle) return wnp_failed_event();
    break;
  }

  int event_id = wnp_get_event_id();
  bool optimistic = false;

// On windows, if a player is a desktop player, execute the event in dp_windows.cpp
// it handles optimistic updates itself.
#ifndef WNP_NODP
  if (player->is_desktop_player) {
    switch (event) {
    case TRY_SET_STATE:
      wnp_dp_try_set_state(player, event_id, data);
      optimistic = true;
      break;
    case TRY_SKIP_PREVIOUS:
      wnp_dp_try_skip_previous(player, event_id);
      break;
    case TRY_SKIP_NEXT:
      wnp_dp_try_skip_next(player, event_id);
      break;
    case TRY_SET_POSITION:
      wnp_dp_try_set_position(player, event_id, data);
      optimistic = true;
      break;
    case TRY_SET_VOLUME:
      wnp_dp_try_set_volume(player, event_id, data);
      optimistic = true;
      break;
    case TRY_SET_RATING:
      wnp_dp_try_set_rating(player, event_id, data);
      optimistic = true;
      break;
    case TRY_SET_REPEAT:
      wnp_dp_try_set_repeat(player, event_id, data);
      optimistic = true;
      break;
    case TRY_SET_SHUFFLE:
      wnp_dp_try_set_shuffle(player, event_id, data);
      optimistic = true;
      break;
    }
    goto on_player_updated;
  }
#endif

  // Optimistic updates for websocket players
  wnp_lock(player);
  switch (event) {
  case TRY_SET_STATE:
    player->state = data;
    optimistic = true;
    break;
  case TRY_SKIP_PREVIOUS:
  case TRY_SKIP_NEXT:
    break;
  case TRY_SET_POSITION:
    player->position = data;
    optimistic = true;
    break;
  case TRY_SET_VOLUME:
    player->volume = data;
    optimistic = true;
    break;
  case TRY_SET_RATING:
    player->rating = data;
    optimistic = true;
    break;
  case TRY_SET_REPEAT:
    player->repeat = data;
    optimistic = true;
    break;
  case TRY_SET_SHUFFLE:
    player->shuffle = data;
    optimistic = true;
    break;
  }
  wnp_unlock(player);

  char msg_buffer[WNP_STR_LEN];
  struct wnp_conn_data* conn_data = wnp_get_conn_data(player);
  if (conn_data != NULL) {
    snprintf(msg_buffer, WNP_STR_LEN - 1, "%d %d %d %d", conn_data->port_id, event_id, event, data);
    cws_send(conn_data->client, msg_buffer, strlen(msg_buffer), CWS_TYPE_TEXT);
  }

on_player_updated:
  if (optimistic) {
    if (g_wnp_events.on_player_updated != NULL) {
      g_wnp_events.on_player_updated(player, g_wnp_events.data);
    }
  }

  return event_id;
}

/**
 * =======================
 * | WebSocket callbacks |
 * =======================
 */

void wnp_ws_on_open(cws_client_t* client)
{
  char buffer[WNP_STR_LEN];
  snprintf(buffer, WNP_STR_LEN - 1, "ADAPTER_VERSION %s;WNPLIB_REVISION 3", g_wnp_adapter_version);
  cws_send(client, buffer, strlen(buffer), CWS_TYPE_TEXT);
}

void wnp_ws_on_close(cws_client_t* client)
{
  thread_mutex_lock(&g_wnp_players_mutex);
  for (size_t i = 0; i < WNP_MAX_PLAYERS; i++) {
    if (g_wnp_players[i].id != -1 && !g_wnp_players[i].is_desktop_player &&
        ((struct wnp_player_data*)g_wnp_players[i]._data)->conn_data->client == client) {
      if (g_wnp_events.on_player_removed != NULL) {
        thread_mutex_unlock(&g_wnp_players_mutex);
        g_wnp_events.on_player_removed(&g_wnp_players[i], g_wnp_events.data);
        thread_mutex_lock(&g_wnp_players_mutex);
      }
      thread_mutex_unlock(&g_wnp_players_mutex);
      wnp_free_player(&g_wnp_players[i]);
      thread_mutex_lock(&g_wnp_players_mutex);
    }
  }
  thread_mutex_unlock(&g_wnp_players_mutex);

  thread_mutex_lock(&g_wnp_cover_buffers_mutex);
  for (size_t i = 0; i < WNP_MAX_COVER_BUFFERS; i++) {
    if (g_wnp_cover_buffers[i] != NULL && g_wnp_cover_buffers[i]->client == client) {
      free(g_wnp_cover_buffers[i]->data);
      free(g_wnp_cover_buffers[i]);
      g_wnp_cover_buffers[i] = NULL;
    }
  }
  thread_mutex_unlock(&g_wnp_cover_buffers_mutex);
}

void wnp_ws_on_message(cws_client_t* client, const unsigned char* _msg, uint64_t msg_size, int type)
{
  if (type == CWS_TYPE_BINARY) {
    size_t id_size = sizeof(uint32_t);
    if (msg_size < id_size) {
      return;
    }
    unsigned int received_id;
    memcpy(&received_id, _msg, id_size);

    uint64_t data_size = msg_size - id_size;
    const unsigned char* data = _msg + id_size;

    struct wnp_player* player = NULL;
    thread_mutex_lock(&g_wnp_players_mutex);
    for (size_t i = 0; i < WNP_MAX_PLAYERS; i++) {
      struct wnp_conn_data* conn_data = wnp_get_conn_data(&g_wnp_players[i]);
      if (conn_data != NULL && conn_data->port_id == received_id && conn_data->client == client) {
        player = &g_wnp_players[i];
        break;
      }
    }
    thread_mutex_unlock(&g_wnp_players_mutex);

    if (player == NULL) {
      thread_mutex_lock(&g_wnp_cover_buffers_mutex);
      for (size_t i = 0; i < WNP_MAX_COVER_BUFFERS; i++) {
        if (g_wnp_cover_buffers[i] == NULL) {
          struct wnp_cover_buffer* cover_buf = calloc(1, sizeof(struct wnp_cover_buffer));
          if (cover_buf == NULL) {
            break;
          }

          cover_buf->client = client;
          cover_buf->data_size = data_size;
          cover_buf->port_id = received_id;
          // need to copy data since it gets freed after onmessage
          cover_buf->data = calloc(1, data_size);
          if (cover_buf->data == NULL) {
            free(cover_buf);
            break;
          }

          memcpy(cover_buf->data, data, data_size);
          g_wnp_cover_buffers[i] = cover_buf;
          break;
        }
      }
      thread_mutex_unlock(&g_wnp_cover_buffers_mutex);
      return;
    }

    wnp_write_cover(player->id, (void*)data, data_size);
    char cover_path[WNP_STR_LEN];
    int ret = wnp_get_cover_path(player->id, cover_path);
    if (ret == 0) {
      wnp_lock(player);
      wnp_assign_str(player->cover, cover_path);
      wnp_unlock(player);
    }
    return;
  }

  char* msg = (char*)_msg;
  char* type_str = strtok(msg, " ");
  if (type_str == NULL) return;
  char* data_str = strtok(NULL, "");
  if (data_str == NULL) return;

  switch (atoi(type_str)) {
  case PLAYER_ADDED: {
    char* id_str = strtok(data_str, " ");
    if (id_str == NULL) return;
    int id = atoi(id_str);

    char* player_text = strtok(NULL, "");
    if (player_text == NULL) return;

    struct wnp_player* player = wnp_create_player();
    if (player == NULL) return;

    struct wnp_conn_data* conn_data = (struct wnp_conn_data*)calloc(1, sizeof(struct wnp_conn_data));
    if (conn_data == NULL) {
      wnp_free_player(player);
      break;
    }

    conn_data->client = client;

    struct wnp_player_data* player_data = player->_data;
    player_data->conn_data = conn_data;

    thread_mutex_lock(&g_wnp_cover_buffers_mutex);
    for (size_t i = 0; i < WNP_MAX_COVER_BUFFERS; i++) {
      if (g_wnp_cover_buffers[i] != NULL && g_wnp_cover_buffers[i]->client == client && g_wnp_cover_buffers[i]->port_id == id) {
        wnp_write_cover(player->id, g_wnp_cover_buffers[i]->data, g_wnp_cover_buffers[i]->data_size);
        char cover_path[WNP_STR_LEN];
        int ret = wnp_get_cover_path(player->id, cover_path);
        if (ret == 0) {
          wnp_lock(player);
          wnp_assign_str(player->cover, cover_path);
          wnp_unlock(player);
        }
        free(g_wnp_cover_buffers[i]->data);
        free(g_wnp_cover_buffers[i]);
        g_wnp_cover_buffers[i] = NULL;
        break;
      }
    }
    thread_mutex_unlock(&g_wnp_cover_buffers_mutex);

    wnp_parse_player_text(player, player_text);
    if (g_wnp_events.on_player_added != NULL) {
      g_wnp_events.on_player_added(player, g_wnp_events.data);
    }
    break;
  }
  case PLAYER_UPDATED: {
    char* id_str = strtok(data_str, " ");
    if (id_str == NULL) return;
    int id = atoi(id_str);

    char* player_text = strtok(NULL, "");
    if (player_text == NULL) return;

    thread_mutex_lock(&g_wnp_players_mutex);
    for (size_t i = 0; i < WNP_MAX_PLAYERS; i++) {
      struct wnp_conn_data* conn_data = wnp_get_conn_data(&g_wnp_players[i]);
      if (conn_data != NULL && conn_data->port_id == id && conn_data->client == client) {
        wnp_parse_player_text(&g_wnp_players[i], player_text);
        if (g_wnp_events.on_player_updated != NULL) {
          thread_mutex_unlock(&g_wnp_players_mutex);
          g_wnp_events.on_player_updated(&g_wnp_players[i], g_wnp_events.data);
          thread_mutex_lock(&g_wnp_players_mutex);
        }
      }
    }
    thread_mutex_unlock(&g_wnp_players_mutex);
    break;
  }
  case PLAYER_REMOVED: {
    int id = atoi(data_str);
    thread_mutex_lock(&g_wnp_players_mutex);
    for (size_t i = 0; i < WNP_MAX_PLAYERS; i++) {
      struct wnp_conn_data* conn_data = wnp_get_conn_data(&g_wnp_players[i]);
      if (conn_data != NULL && conn_data->port_id == id && conn_data->client == client) {
        thread_mutex_unlock(&g_wnp_players_mutex);
        if (g_wnp_events.on_player_removed != NULL) {
          g_wnp_events.on_player_removed(&g_wnp_players[i], g_wnp_events.data);
        }
        wnp_free_player(&g_wnp_players[i]);
        thread_mutex_lock(&g_wnp_players_mutex);
      }
    }
    thread_mutex_unlock(&g_wnp_players_mutex);
    break;
  }
  case EVENT_RESULT: {
    char* event_id_str = strtok(data_str, " ");
    if (event_id_str == NULL) return;

    char* event_result_str = strtok(NULL, "");
    if (event_result_str == NULL) return;

    int event_id = atoi(event_id_str);
    if (event_id > WNP_MAX_EVENT_RESULTS - 1 || event_id < 0) return;

    wnp_set_event_result(event_id, atoi(event_result_str));
    break;
  }
  case USE_DESKTOP_PLAYERS: {
#ifndef WNP_NODP
    if (wnp_set_use_dp(atoi(data_str))) {
      wnp_dp_start();
    } else {
      wnp_dp_stop();
    }
#endif
    break;
  }
  }

  wnp_recalculate_active_player();
}

/**
 * =======================
 * | some stuff or so ye |
 * =======================
 */

void wnp_init_globals(bool start)
{
  if (!start) {
    thread_mutex_lock(&g_wnp_players_mutex);
    thread_mutex_lock(&g_wnp_cover_buffers_mutex);
    thread_mutex_lock(&g_wnp_event_results_mutex);
  }

  g_wnp_use_dp = wnp_read_use_dp();
  strncpy(g_wnp_adapter_version, "0.0.0", 5);
  // memset not to initialize but to wipe the struct
  memset(&g_wnp_events, 0, sizeof(g_wnp_events));

  if (start) {
    thread_mutex_init(&g_wnp_players_mutex);
    thread_mutex_init(&g_wnp_cover_buffers_mutex);
    thread_mutex_init(&g_wnp_event_results_mutex);
  } else {
    thread_mutex_term(&g_wnp_players_mutex);
    thread_mutex_term(&g_wnp_cover_buffers_mutex);
    thread_mutex_term(&g_wnp_event_results_mutex);
  }

  for (size_t i = 0; i < WNP_MAX_PLAYERS; i++) {
    g_wnp_players[i] = WNP_DEFAULT_PLAYER;
  }

  for (size_t i = 0; i < WNP_MAX_EVENT_RESULTS; i++) {
    g_wnp_event_results[i] = WNP_EVENT_PENDING;
  }

  for (size_t i = 0; i < WNP_MAX_COVER_BUFFERS; i++) {
    if (g_wnp_cover_buffers[i] != NULL) {
      free(g_wnp_cover_buffers[i]->data);
      free(g_wnp_cover_buffers[i]);
      g_wnp_cover_buffers[i] = NULL;
    }
  }
}

bool wnp_is_valid_adapter_version(const char* adapter_version)
{
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

/**
 * ========================
 * | Public api functions |
 * ========================
 */

int wnp_start(int port, const char* adapter_version, struct wnp_events* events)
{
  wnp_init_globals(true);

  if (wnp_is_started()) {
    return 1;
  }

  if (!wnp_is_valid_adapter_version(adapter_version)) {
    return 3;
  }

  strncpy((char*)g_wnp_adapter_version, adapter_version, 5);

  if (events != NULL) {
    memcpy(&g_wnp_events, events, sizeof(struct wnp_events));
  }

#ifndef WNP_NODP
  wnp_dp_start();
#endif

  int ret = cws_start((cws_server_t){
      .port = port,
      .on_open = &wnp_ws_on_open,
      .on_close = &wnp_ws_on_close,
      .on_message = &wnp_ws_on_message,
  });

  if (ret != 0) {
    return 2;
  }

  g_wnp_is_started = true;

  return 0;
}

int wnp_stop()
{
  if (!wnp_is_started()) {
    return 1;
  }

  g_wnp_is_started = false;

#ifndef WNP_NODP
  wnp_dp_stop();
#endif

  thread_mutex_lock(&g_wnp_players_mutex);
  if (g_wnp_events.on_player_removed != NULL) {
    for (size_t i = 0; i < WNP_MAX_PLAYERS; i++) {
      if (g_wnp_players[i].id != -1) {
        thread_mutex_unlock(&g_wnp_players_mutex);
        g_wnp_events.on_player_removed(&g_wnp_players[i], g_wnp_events.data);
        thread_mutex_lock(&g_wnp_players_mutex);
      }
    }
  }
  thread_mutex_unlock(&g_wnp_players_mutex);

  wnp_recalculate_active_player();
  wnp_init_globals(false);
  cws_stop();
  return 0;
}

bool wnp_is_started()
{
  return g_wnp_is_started;
}

bool wnp_get_use_dp()
{
  return g_wnp_use_dp;
}

void wnp_lock(struct wnp_player* player)
{
  struct wnp_player_data* player_data = player->_data;
  if (player_data != NULL) {
    thread_mutex_lock(&player_data->lock);
  }
}

void wnp_unlock(struct wnp_player* player)
{
  struct wnp_player_data* player_data = player->_data;
  if (player_data != NULL) {
    thread_mutex_unlock(&player_data->lock);
  }
}

struct wnp_player* wnp_get_player(int id, bool always_return_player)
{
  if (id < 0 || id >= WNP_MAX_PLAYERS) {
    if (always_return_player) {
      return &WNP_DEFAULT_PLAYER;
    } else {
      return NULL;
    }
  }

  thread_mutex_lock(&g_wnp_players_mutex);
  if (g_wnp_players[id].id != id && always_return_player) {
    thread_mutex_unlock(&g_wnp_players_mutex);
    return &WNP_DEFAULT_PLAYER;
  }
  struct wnp_player* player = &g_wnp_players[id];
  thread_mutex_unlock(&g_wnp_players_mutex);

  return player;
}

struct wnp_player* wnp_get_active_player(bool always_return_player)
{
  if (g_wnp_active_player_id == -1) {
    if (always_return_player) {
      return &WNP_DEFAULT_PLAYER;
    } else {
      return NULL;
    }
  }

  thread_mutex_lock(&g_wnp_players_mutex);
  if (g_wnp_players[g_wnp_active_player_id].id == -1) {
    thread_mutex_unlock(&g_wnp_players_mutex);
    g_wnp_active_player_id = -1;
    if (always_return_player) {
      return &WNP_DEFAULT_PLAYER;
    } else {
      return NULL;
    }
  }
  struct wnp_player* player = &g_wnp_players[g_wnp_active_player_id];
  thread_mutex_unlock(&g_wnp_players_mutex);

  return player;
}

int wnp_get_all_players(struct wnp_player* players[WNP_MAX_PLAYERS])
{
  int pi = 0;

  thread_mutex_lock(&g_wnp_players_mutex);
  for (int i = 0; i < WNP_MAX_PLAYERS; i++) {
    if (g_wnp_players[i].id != -1) {
      players[pi] = &g_wnp_players[i];
      pi++;
    }
  }
  thread_mutex_unlock(&g_wnp_players_mutex);

  return pi;
}

enum wnp_event_result wnp_get_event_result(int event_id)
{
  if (event_id < 0 || event_id >= WNP_MAX_EVENT_RESULTS) {
    return WNP_EVENT_FAILED;
  }

  thread_mutex_lock(&g_wnp_event_results_mutex);
  int event_result = g_wnp_event_results[event_id];
  thread_mutex_unlock(&g_wnp_event_results_mutex);

  return event_result;
}

enum wnp_event_result wnp_wait_for_event_result(int event_id)
{
  if (event_id < 0 || event_id >= WNP_MAX_EVENT_RESULTS) {
    return WNP_EVENT_FAILED;
  }

  int count = 0;
  int max_count = 100;
  while (wnp_get_event_result(event_id) == WNP_EVENT_PENDING) {
    if (count > max_count) {
      wnp_set_event_result(event_id, WNP_EVENT_FAILED);
      break;
    }
    count++;
#ifdef _WIN32
    Sleep(10);
#else
    usleep(10 * 1000);
#endif
  }

  return wnp_get_event_result(event_id);
}

float wnp_get_position_percent(struct wnp_player* player)
{
  if (player->duration == 0) return 100.0;
  return ((float)(player->position) / player->duration) * 100.0;
}

int wnp_get_remaining_seconds(struct wnp_player* player)
{
  return player->duration - player->position;
}

void wnp_format_seconds(int seconds, bool pad_with_zeroes, char out_str[10])
{
  int hours = seconds / 3600;
  int remainder = seconds % 3600;
  int minutes = remainder / 60;
  int secs = remainder % 60;

  if (hours >= 1) {
    if (pad_with_zeroes) {
      snprintf(out_str, 10, "%02d:%02d:%02d", hours, minutes, secs);
    } else {
      snprintf(out_str, 10, "%d:%02d:%02d", hours, minutes, secs);
    }
  } else {
    if (pad_with_zeroes) {
      snprintf(out_str, 10, "%02d:%02d", minutes, secs);
    } else {
      snprintf(out_str, 10, "%d:%02d", minutes, secs);
    }
  }
}

/**
 * ===============================
 * | Public base event functions |
 * ===============================
 */

int wnp_try_set_state(struct wnp_player* player, enum wnp_state state)
{
  return wnp_execute_event(player, TRY_SET_STATE, state);
}

int wnp_try_skip_previous(struct wnp_player* player)
{
  return wnp_execute_event(player, TRY_SKIP_PREVIOUS, 0);
}

int wnp_try_skip_next(struct wnp_player* player)
{
  return wnp_execute_event(player, TRY_SKIP_NEXT, 0);
}

int wnp_try_set_position(struct wnp_player* player, int seconds)
{
  if (player->duration == 0) return wnp_failed_event();
  if (seconds < 0) seconds = 0;
  if (seconds > player->duration) seconds = player->duration;

  return wnp_execute_event(player, TRY_SET_POSITION, seconds);
}

int wnp_try_set_volume(struct wnp_player* player, int volume)
{
  if (volume < 0) volume = 0;
  if (volume > 100) volume = 100;
  return wnp_execute_event(player, TRY_SET_VOLUME, volume);
}

int wnp_try_set_rating(struct wnp_player* player, int rating)
{
  return wnp_execute_event(player, TRY_SET_RATING, rating);
}

int wnp_try_set_repeat(struct wnp_player* player, enum wnp_repeat repeat)
{
  return wnp_execute_event(player, TRY_SET_REPEAT, repeat);
}

int wnp_try_set_shuffle(struct wnp_player* player, bool shuffle)
{
  return wnp_execute_event(player, TRY_SET_SHUFFLE, shuffle);
}

/**
 * ===============================
 * | Public util event functions |
 * ===============================
 */

int wnp_try_play_pause(struct wnp_player* player)
{
  int is_paused = player->state == WNP_STATE_PAUSED || player->state == WNP_STATE_STOPPED;
  return wnp_try_set_state(player, is_paused ? WNP_STATE_PLAYING : WNP_STATE_PAUSED);
}

int wnp_try_revert(struct wnp_player* player, int seconds)
{
  return wnp_try_set_position(player, player->position - seconds);
}

int wnp_try_forward(struct wnp_player* player, int seconds)
{
  return wnp_try_set_position(player, player->position + seconds);
}

int wnp_try_set_position_percent(struct wnp_player* player, float percent)
{
  int seconds = (int)(percent / 100 * player->duration + 0.5);
  return wnp_try_set_position(player, seconds);
}

int wnp_try_revert_percent(struct wnp_player* player, float percent)
{
  int seconds = (int)(percent / 100 * player->duration + 0.5);
  return wnp_try_set_position(player, player->position - seconds);
}

int wnp_try_forward_percent(struct wnp_player* player, float percent)
{
  int seconds = (int)(percent / 100 * player->duration + 0.5);
  return wnp_try_set_position(player, player->position + seconds);
}

int wnp_try_toggle_repeat(struct wnp_player* player)
{
  enum wnp_repeat next_repeat = player->repeat;
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
    return wnp_failed_event();
  }

  return wnp_try_set_repeat(player, next_repeat);
}
