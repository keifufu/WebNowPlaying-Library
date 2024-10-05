#ifdef WNP_BUILD_PLATFORM_WEB

#include "cws.h"
#include "internal.h"
#include "thread.h"
#include "wnp.h"

/**
 * ================================
 * | Definitions and global state |
 * ================================
 */

typedef enum {
  WNP_PLAYER_ADDED = 0,
  WNP_PLAYER_UPDATED = 1,
  WNP_PLAYER_REMOVED = 2,
  WNP_EVENT_RESULT = 3,
} _web_message_type_t;

typedef struct {
  void* dest;
  int type; // 0 = char[WNP_STR_LEN], 1 = int, 2 = uint64_t
} _web_field_action_t;

typedef struct {
  cws_client_t* client;
  int port_id;
} _web_platform_data_t;

typedef struct {
  void* data;
  uint64_t data_size;
  cws_client_t* client;
  int port_id;
} _web_cover_buffer_t;

#define WNP_MAX_COVER_BUFFERS WNP_MAX_PLAYERS
typedef struct {
  wnp_args_t args;
  _web_cover_buffer_t* cover_buffers[WNP_MAX_COVER_BUFFERS];
  thread_mutex_t cover_buffers_lock;
} _web_state_t;

static _web_state_t _web_state = {0};

/**
 * ==============================
 * | Private internal functions |
 * ==============================
 */

static _web_platform_data_t* _web_get_platform_data(wnp_player_t* player)
{
  if (player == NULL || player->platform != WNP_PLATFORM_WEB) {
    return NULL;
  }

  return (_web_platform_data_t*)player->_platform_data;
}

static void _web_assign_str(char dest[WNP_STR_LEN], const char* str)
{
  size_t len = strlen(str);
  strncpy(dest, str, WNP_STR_LEN - 1);
  dest[len < WNP_STR_LEN ? len : WNP_STR_LEN - 1] = '\0';
}

static void _web_parse_player_text(wnp_player_t* player, char* data)
{
  const _web_field_action_t actions[] = {
      {&(((_web_platform_data_t*)player->_platform_data)->port_id), 1},
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

  while (field_counter < field_len) {
    if (*p == '\\' && *(p + 1) == '|') {
      p++;
    } else if (*p == '\0' || *p == '|') {
      if (*p == '\0') break; // End of data

      *p = '\0'; // Terminate the token
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
        const _web_field_action_t* action = &actions[field_counter];
        switch (action->type) {
          case 0:
            _web_assign_str(action->dest, (*token == '\1') ? "" : token);
            break;
          case 1:
            *(int*)action->dest = atoi(token);
            break;
          case 2:
            *(uint64_t*)action->dest = strtoll(token, NULL, 10);
            break;
        }
      }

      token_start = p + 1;
      field_counter++;
    }
    p++;
  }
}

/**
 * =======================
 * | WebSocket callbacks |
 * =======================
 */

void _web_ws_on_open(cws_client_t* client)
{
  char buffer[WNP_STR_LEN] = {0};
  snprintf(buffer, WNP_STR_LEN - 1, "ADAPTER_VERSION %s,WNPLIB_REVISION 3", _web_state.args.adapter_version);
  cws_send(client, buffer, strlen(buffer), CWS_TYPE_TEXT);
}

void _web_ws_on_close(cws_client_t* client)
{
  wnp_player_t players[WNP_MAX_PLAYERS] = {0};
  int count = __wnp_start_update_cycle(players);
  for (size_t i = 0; i < count; i++) {
    _web_platform_data_t* platform_data = _web_get_platform_data(&players[i]);
    if (platform_data != NULL && platform_data->client == client) {
      __wnp_remove_player(players[i].id);
    }
  }
  __wnp_end_update_cycle();

  thread_mutex_lock(&_web_state.cover_buffers_lock);
  for (size_t i = 0; i < WNP_MAX_COVER_BUFFERS; i++) {
    if (_web_state.cover_buffers[i] != NULL && _web_state.cover_buffers[i]->client == client) {
      free(_web_state.cover_buffers[i]->data);
      free(_web_state.cover_buffers[i]);
      _web_state.cover_buffers[i] = NULL;
    }
  }
  thread_mutex_unlock(&_web_state.cover_buffers_lock);
}

void _web_ws_on_message(cws_client_t* client, const unsigned char* _msg, uint64_t msg_size, int type)
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

    wnp_player_t* player = NULL;
    wnp_player_t players[WNP_MAX_PLAYERS] = {0};
    int count = __wnp_start_update_cycle(players);
    for (size_t i = 0; i < count; i++) {
      _web_platform_data_t* platform_data = _web_get_platform_data(&players[i]);
      if (platform_data != NULL && platform_data->port_id == received_id && platform_data->client == client) {
        player = &players[i];
        break;
      }
    }

    if (player == NULL) {
      __wnp_end_update_cycle();
      thread_mutex_lock(&_web_state.cover_buffers_lock);
      for (size_t i = 0; i < WNP_MAX_COVER_BUFFERS; i++) {
        if (_web_state.cover_buffers[i] == NULL) {
          _web_cover_buffer_t* cover_buffer = calloc(1, sizeof(_web_cover_buffer_t));
          if (cover_buffer == NULL) {
            break;
          }

          cover_buffer->client = client;
          cover_buffer->data_size = data_size;
          cover_buffer->port_id = received_id;
          // need to copy data since it gets freed after onmessage
          cover_buffer->data = calloc(1, data_size);
          if (cover_buffer->data == NULL) {
            free(cover_buffer);
            break;
          }

          memcpy(cover_buffer->data, data, data_size);
          _web_state.cover_buffers[i] = cover_buffer;
          break;
        }
      }
      thread_mutex_unlock(&_web_state.cover_buffers_lock);
      return;
    }

    __wnp_write_cover(player->id, (void*)data, data_size);
    char cover_path[WNP_STR_LEN] = {0};
    if (__wnp_get_cover_path(player->id, cover_path)) {
      _web_assign_str(player->cover, cover_path);
      __wnp_update_player(player);
      __wnp_end_update_cycle();
    }
    return;
  }

  char* msg = (char*)_msg;
  char* type_str = strtok(msg, " ");
  if (type_str == NULL) return;
  char* data_str = strtok(NULL, "");
  if (data_str == NULL) return;

  switch (atoi(type_str)) {
    case WNP_PLAYER_ADDED: {
      char* id_str = strtok(data_str, " ");
      if (id_str == NULL) return;
      int id = atoi(id_str);

      char* player_text = strtok(NULL, "");
      if (player_text == NULL) return;

      _web_platform_data_t* platform_data = (_web_platform_data_t*)calloc(1, sizeof(_web_platform_data_t));
      if (platform_data == NULL) return;

      platform_data->client = client;

      wnp_player_t player = WNP_DEFAULT_PLAYER;
      player._platform_data = platform_data;
      player.platform = WNP_PLATFORM_WEB;

      __wnp_start_update_cycle(NULL);
      player.id = __wnp_add_player(&player);
      if (player.id == -1) {
        free(platform_data);
        return;
      }

      thread_mutex_lock(&_web_state.cover_buffers_lock);
      for (size_t i = 0; i < WNP_MAX_COVER_BUFFERS; i++) {
        _web_cover_buffer_t* buf = _web_state.cover_buffers[i];
        if (buf != NULL && buf->client == client && buf->port_id == id) {
          __wnp_write_cover(player.id, buf->data, buf->data_size);
          char cover_path[WNP_STR_LEN] = {0};
          if (__wnp_get_cover_path(player.id, cover_path)) {
            _web_assign_str(player.cover, cover_path);
          }
          free(buf->data);
          free(buf);
          _web_state.cover_buffers[i] = NULL;
          break;
        }
      }
      thread_mutex_unlock(&_web_state.cover_buffers_lock);

      _web_parse_player_text(&player, player_text);
      __wnp_update_player(&player);
      __wnp_end_update_cycle();

      break;
    }
    case WNP_PLAYER_UPDATED: {
      char* id_str = strtok(data_str, " ");
      if (id_str == NULL) return;
      int id = atoi(id_str);

      char* player_text = strtok(NULL, "");
      if (player_text == NULL) return;

      wnp_player_t players[WNP_MAX_PLAYERS] = {0};
      int count = __wnp_start_update_cycle(players);
      for (size_t i = 0; i < count; i++) {
        _web_platform_data_t* platform_data = _web_get_platform_data(&players[i]);
        if (platform_data != NULL && platform_data->port_id == id && platform_data->client == client) {
          _web_parse_player_text(&players[i], player_text);
          __wnp_update_player(&players[i]);
        }
      }
      __wnp_end_update_cycle();
      break;
    }
    case WNP_PLAYER_REMOVED: {
      int id = atoi(data_str);

      wnp_player_t players[WNP_MAX_PLAYERS] = {0};
      int count = __wnp_start_update_cycle(players);
      for (size_t i = 0; i < count; i++) {
        _web_platform_data_t* platform_data = _web_get_platform_data(&players[i]);
        if (platform_data != NULL && platform_data->port_id == id && platform_data->client == client) {
          __wnp_remove_player(players[i].id);
        }
      }
      __wnp_end_update_cycle();
      break;
    }
    case WNP_EVENT_RESULT: {
      char* event_id_str = strtok(data_str, " ");
      if (event_id_str == NULL) return;

      char* event_result_str = strtok(NULL, "");
      if (event_result_str == NULL) return;

      int event_id = atoi(event_id_str);
      if (event_id > WNP_MAX_EVENT_RESULTS - 1 || event_id < 0) return;

      __wnp_set_event_result(event_id, atoi(event_result_str));
      break;
    }
  }
}

/**
 * =============================
 * | Shared internal functions |
 * =============================
 */

wnp_init_ret_t __wnp_platform_web_init()
{
  __wnp_get_args(&_web_state.args);

  int ret = cws_start((cws_server_t){
      .port = _web_state.args.web_port,
      .on_open = &_web_ws_on_open,
      .on_close = &_web_ws_on_close,
      .on_message = &_web_ws_on_message,
  });

  if (ret != 0) {
    return WNP_INIT_WEB_PORT_IN_USE;
  }

  for (size_t i = 0; i < WNP_MAX_COVER_BUFFERS; i++) {
    _web_state.cover_buffers[i] = NULL;
  }
  thread_mutex_init(&_web_state.cover_buffers_lock);

  return WNP_INIT_SUCCESS;
}

void __wnp_platform_web_uninit()
{
  cws_stop();
  memset(&_web_state.args, 0, sizeof(wnp_args_t));
  for (size_t i = 0; i < WNP_MAX_COVER_BUFFERS; i++) {
    _web_state.cover_buffers[i] = NULL;
  }
  thread_mutex_term(&_web_state.cover_buffers_lock);
}

void __wnp_platform_web_free(void* _platform_data)
{
  _web_platform_data_t* platform_data = (_web_platform_data_t*)_platform_data;

  if (platform_data != NULL) {
    free(platform_data);
  }
}

void __wnp_platform_web_event(wnp_player_t* player, wnp_event_t event, int event_id, int data)
{
  _web_platform_data_t* platform_data = _web_get_platform_data(player);
  if (platform_data == NULL) return;

  char msg_buffer[WNP_STR_LEN] = {0};
  snprintf(msg_buffer, WNP_STR_LEN - 1, "%d %d %d %d", platform_data->port_id, event_id, event, data);
  cws_send(platform_data->client, msg_buffer, strlen(msg_buffer), CWS_TYPE_TEXT);

  switch (event) {
    case WNP_TRY_SET_STATE:
      player->state = data;
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
      player->repeat = data;
      break;
    case WNP_TRY_SET_SHUFFLE:
      player->shuffle = data;
      break;
  }
}

#endif /* WNP_BUILD_PLATFORM_WEB */
