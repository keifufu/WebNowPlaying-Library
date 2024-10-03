#ifdef WNP_BUILD_PLATFORM_LINUX

#include "internal.h"
#include "stb_image.h"
#include "stb_image_write.h"
#include "wnp.h"

#include <ctype.h>
#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>

/**
 * ================================
 * | Definitions and global state |
 * ================================
 */

typedef struct {
  guint signal_subscription_id;
  GDBusConnection* connection;
  thread_ptr_t loop_thread;
  GMainLoop* loop;
} _linux_state_t;

typedef struct {
  gchar* player_name;
  guint signal_subscription_id;
} _linux_platform_data_t;

static _linux_state_t _linux_state = {0};

/**
 * ==============================
 * | Private internal functions |
 * ==============================
 */

static uint64_t _linux_timestamp() {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  uint64_t milliseconds = (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
  return milliseconds;
}

static _linux_platform_data_t* _linux_get_platform_data(wnp_player_t* player) {
  if (player == NULL || player->platform != WNP_PLATFORM_LINUX) {
    return NULL;
  }

  return (_linux_platform_data_t*)player->_platform_data;
}

static void _linux_assign_str(char dest[WNP_STR_LEN], const char* str) {
  if (str == NULL) return;
  size_t len = strlen(str);
  strncpy(dest, str, WNP_STR_LEN - 1);
  dest[len < WNP_STR_LEN ? len : WNP_STR_LEN - 1] = '\0';
}

static bool _linux_is_web_browser(const gchar* _player_name) {
  char player_name[WNP_STR_LEN] = {0};
  _linux_assign_str(player_name, _player_name);
  for (char* p = player_name; *p; ++p) {
    *p = tolower(*p);
  }

  if (strstr(player_name, "firefox") != NULL)
    return true;
  else if (strstr(player_name, "chrom") != NULL) // chrom-* (chromium, chrome (if ever))
    return true;
  else
    return false;
}

static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static inline bool is_base64(char c) {
  return isalnum(c) || c == '+' || c == '/';
}

static int base64_chars_find(char find) {
  for (size_t i = 0; i < 64; ++i) {
    if (base64_chars[i] == find) {
      return i;
    }
  }

  return -1;
}

static size_t decode_base64(const char* string, void* data) {
  size_t size = 0;
  uint8_t* ptr = (uint8_t*)data;
  size_t len = strlen(string);

  uint8_t char_array_3[3];
  uint8_t char_array_4[4];

  size_t idx = 0;
  size_t i = 0;
  size_t j = 0;

  while (idx < len && string[idx] != '=' && is_base64(string[idx])) {
    char_array_4[i++] = string[idx];
    ++idx;

    if (i == 4) {
      for (i = 0; i < 4; ++i) {
        char_array_4[i] = (uint8_t)base64_chars_find(char_array_4[i]);
        if (char_array_4[i] == (uint8_t)-1) {
          return size;
        }
      }

      char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
      char_array_3[1] = ((char_array_4[1] & 0x0F) << 4) + ((char_array_4[2] & 0x3C) >> 2);
      char_array_3[2] = ((char_array_4[2] & 0x03) << 6) + char_array_4[3];

      for (i = 0; i < 3; ++i, ++ptr) {
        *ptr = char_array_3[i];
        ++size;
      }

      i = 0;
    }
  }

  if (i > 0) {
    for (j = i; j < 4; ++j) {
      char_array_4[j] = 0;
    }

    for (j = 0; j < 4; ++j) {
      char_array_4[j] = (uint8_t)base64_chars_find(char_array_4[j]);
      if (char_array_4[j] == (uint8_t)-1) {
        return size;
      }
    }

    char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
    char_array_3[1] = ((char_array_4[1] & 0x0F) << 4) + ((char_array_4[2] & 0x3C) >> 2);
    char_array_3[2] = ((char_array_4[2] & 0x03) << 6) + char_array_4[3];

    for (j = 0; j < i - 1; ++j, ++ptr) {
      *ptr = char_array_3[j];
      ++size;
    }
  }

  return size;
}

static size_t base64_decode_bytes(const char* string) {
  size_t size = 0;
  size_t len = strlen(string);
  size_t idx = 0;
  size_t i = 0;

  while (idx < len && string[idx] != '=' && is_base64(string[idx])) {
    ++idx;
    ++i;
    if (i == 4) {
      i = 0;
      size += 3;
    }
  }

  if (i > 0) {
    size += i - 1;
  }

  return size;
}

static void _linux_parse_metadata(wnp_player_t* player, GVariant* metadata) {
  GVariantIter iter;
  const gchar* key;
  GVariant* value;

  g_variant_iter_init(&iter, metadata);

  while (g_variant_iter_next(&iter, "{sv}", &key, &value)) {
    if (g_strcmp0(key, "mpris:length") == 0) {
      player->duration = g_variant_get_int64(value) / 1000000;
    } else if (g_strcmp0(key, "mpris:artUrl") == 0) {
      char cover_path[WNP_STR_LEN] = {0};
      if (__wnp_get_cover_path(player->id, cover_path)) {
        const gchar* art_url = g_variant_get_string(value, NULL);

        if (g_str_has_prefix(art_url, "file://")) {
          int width, height, channels;
          unsigned char* image_data = stbi_load(art_url + 7, &width, &height, &channels, 0);
          if (image_data == NULL) {
            break;
          }

          stbi_write_png(cover_path + 7, width, height, channels, image_data, width * channels);
          stbi_image_free(image_data);

          _linux_assign_str(player->cover, cover_path);
          _linux_assign_str(player->cover_src, art_url);
        } else if (g_str_has_prefix(art_url, "data:image")) {
          const char* cover_src = strtok((char*)art_url, ",");
          const char* uri = strtok(NULL, ",");

          size_t size = base64_decode_bytes(uri);
          void* data = malloc(size);
          if (data == NULL) break;
          decode_base64(uri, data);

          int width, height, channels;
          unsigned char* image_data = stbi_load_from_memory(data, size, &width, &height, &channels, 0);
          if (image_data == NULL) {
            free(data);
            break;
          }

          stbi_write_png(cover_path + 7, width, height, channels, image_data, width * channels);
          stbi_image_free(image_data);

          free(data);
          _linux_assign_str(player->cover, cover_path);
          _linux_assign_str(player->cover_src, cover_src);
        }
      } else {
        _linux_assign_str(player->cover, "");
        _linux_assign_str(player->cover_src, "");
      }
    } else if (g_strcmp0(key, "xesam:album") == 0) {
      _linux_assign_str(player->album, g_variant_get_string(value, NULL));
    } else if (g_strcmp0(key, "xesam:artist") == 0) {
      GVariantIter artist_iter;
      const gchar* artist_name;

      g_variant_iter_init(&artist_iter, value);
      GString* artist_concat = g_string_new("");

      while (g_variant_iter_next(&artist_iter, "s", &artist_name)) {
        if (artist_concat->len > 0) {
          g_string_append(artist_concat, ", ");
        }
        g_string_append(artist_concat, artist_name);
      }

      _linux_assign_str(player->artist, artist_concat->str);
      g_string_free(artist_concat, TRUE);
    } else if (g_strcmp0(key, "xesam:title") == 0) {
      const gchar* new_title = g_variant_get_string(value, NULL);
      if (new_title != NULL) {
        if (g_strcmp0(player->title, new_title) != 0) {
          _linux_assign_str(player->title, new_title);
          if (strlen(new_title) == 0) {
            player->active_at = 0;
          } else {
            player->active_at = _linux_timestamp();
          }
        }
      }
    } else if (g_strcmp0(key, "xesam:userRating") == 0) {
      gdouble user_rating = g_variant_get_double(value);
      player->rating = (int)(user_rating * 5 + 0.5);
    }
    g_variant_unref(value);
  }
}

static void _linux_parse_properties(wnp_player_t* player, GVariant* properties) {
  GVariantIter iter;
  const gchar* key;
  GVariant* value;

  gboolean can_play = false;
  gboolean can_pause = false;

  g_variant_iter_init(&iter, properties);

  while (g_variant_iter_next(&iter, "{sv}", &key, &value)) {
    if (g_strcmp0(key, "PlaybackStatus") == 0) {
      const gchar* playback_status = g_variant_get_string(value, NULL);
      if (playback_status != NULL) {
        wnp_state_t new_state = player->state;
        if (g_strcmp0(playback_status, "Playing") == 0) {
          new_state = WNP_STATE_PLAYING;
        } else if (g_strcmp0(playback_status, "Paused") == 0) {
          new_state = WNP_STATE_PAUSED;
        } else {
          new_state = WNP_STATE_STOPPED;
        }

        if (new_state != player->state) {
          player->state = new_state;
          player->active_at = _linux_timestamp();
        }
      }
    } else if (g_strcmp0(key, "LoopStatus") == 0) {
      const gchar* loop_status = g_variant_get_string(value, NULL);
      if (loop_status != NULL) {
        if (g_strcmp0(loop_status, "None") == 0) {
          player->repeat = WNP_REPEAT_NONE;
        } else if (g_strcmp0(loop_status, "Track") == 0) {
          player->repeat = WNP_REPEAT_ONE;
        } else if (g_strcmp0(loop_status, "Playlist") == 0) {
          player->repeat = WNP_REPEAT_ALL;
        }
      }
    } else if (g_strcmp0(key, "Shuffle") == 0) {
      player->shuffle = g_variant_get_boolean(value);
    } else if (g_strcmp0(key, "Metadata") == 0) {
      _linux_parse_metadata(player, value);
    } else if (g_strcmp0(key, "Volume") == 0) {
      player->volume = g_variant_get_double(value) * 100;
    } else if (g_strcmp0(key, "Position") == 0) {
      player->position = g_variant_get_int64(value) / 1000000;
    } else if (g_strcmp0(key, "CanGoNext") == 0) {
      player->can_skip_next = g_variant_get_boolean(value);
    } else if (g_strcmp0(key, "CanPlay") == 0) {
      can_play = g_variant_get_boolean(value);
    } else if (g_strcmp0(key, "CanPause") == 0) {
      can_pause = g_variant_get_boolean(value);
    } else if (g_strcmp0(key, "CanSeek") == 0) {
      player->can_set_position = g_variant_get_boolean(value);
    } else if (g_strcmp0(key, "CanControl") == 0) {
      if (g_variant_get_boolean(value) == false) {
        player->can_set_state = false;
        player->can_skip_previous = false;
        player->can_skip_next = false;
        player->can_set_position = false;
        player->can_set_volume = false;
        player->can_set_rating = false;
        player->can_set_repeat = false;
        player->can_set_shuffle = false;
      }
    }
    g_variant_unref(value);
  }

  if (can_play || can_pause) {
    player->can_set_state = true;
  }

  player->updated_at = _linux_timestamp();
}

// clang-format off
static void _linux_on_properties_changed(
  GDBusConnection* connection,
  const gchar* sender_name,
  const gchar* object_path,
  const gchar* interface_name,
  const gchar* signal_name,
  GVariant* parameters,
  gpointer user_data
) {
  // clang-format on
  gchar* player_name = (gchar*)user_data;

  wnp_player_t players[WNP_MAX_PLAYERS] = {0};
  int count = __wnp_start_update_cycle(players);

  wnp_player_t* player = NULL;
  for (size_t i = 0; i < WNP_MAX_PLAYERS; i++) {
    _linux_platform_data_t* platform_data = _linux_get_platform_data(&players[i]);
    if (platform_data != NULL && g_strcmp0(platform_data->player_name, player_name) == 0) {
      player = &players[i];
      break;
    }
  }

  if (player == NULL) {
    __wnp_end_update_cycle();
    return;
  }

  gchar* iface;
  GVariant* changed_properties;
  g_variant_get(parameters, "(&s@a{sv}@as)", &iface, &changed_properties, NULL);
  if (g_strcmp0(iface, "org.mpris.MediaPlayer2.Player") == 0) {
    _linux_parse_properties(player, changed_properties);
    __wnp_update_player(player);
  }

  __wnp_end_update_cycle();
  g_variant_unref(changed_properties);
}

static void _linux_player_added(GDBusConnection* connection, const gchar* player_name) {
  wnp_player_t players[WNP_MAX_PLAYERS] = {0};
  int count = __wnp_start_update_cycle(players);

  for (size_t i = 0; i < count; i++) {
    _linux_platform_data_t* platform_data = _linux_get_platform_data(&players[i]);
    if (platform_data != NULL && g_strcmp0(platform_data->player_name, player_name) == 0) {
      __wnp_end_update_cycle();
      return;
    }
  }

  _linux_platform_data_t* platform_data = (_linux_platform_data_t*)calloc(1, sizeof(_linux_platform_data_t));
  if (platform_data == NULL) {
    __wnp_end_update_cycle();
    return;
  }

  wnp_player_t player = WNP_DEFAULT_PLAYER;
  player._platform_data = platform_data;
  player.platform = WNP_PLATFORM_LINUX;
  player.is_web_browser = _linux_is_web_browser(player_name);

  platform_data->player_name = g_strdup(player_name);

  player.id = __wnp_add_player(&player);
  if (player.id == -1) {
    free(platform_data);
    __wnp_end_update_cycle();
    return;
  }

  if (strlen(player_name) > 23) {
    // skip "org.mpris.MediaPlayer2."
    // and truncate second dot (e.g. firefox.instance_bla_bla)
    char _player_name[WNP_STR_LEN] = {0};
    _linux_assign_str(_player_name, player_name + 23);
    char* dot = strtok(_player_name, ".");
    if (dot == NULL) {
      _linux_assign_str(player.name, player_name + 23);
    } else {
      _linux_assign_str(player.name, dot);
    }
  } else {
    _linux_assign_str(player.name, player_name);
  }
  player.rating_system = WNP_RATING_SYSTEM_SCALE;
  player.available_repeat = WNP_REPEAT_NONE | WNP_REPEAT_ONE | WNP_REPEAT_ALL;
  player.created_at = _linux_timestamp();
  player.updated_at = player.created_at;
  player.can_set_state = true;
  player.can_skip_previous = true;
  player.can_skip_next = true;
  player.can_set_position = true;
  player.can_set_volume = true;
  player.can_set_rating = true;
  player.can_set_repeat = true;
  player.can_set_shuffle = true;

  GVariant* reply = NULL;
  GError* error = NULL;
  // clang-format off
  reply = g_dbus_connection_call_sync(
    connection,
    player_name,
    "/org/mpris/MediaPlayer2",
    "org.freedesktop.DBus.Properties",
    "GetAll",
    g_variant_new("(s)", "org.mpris.MediaPlayer2.Player"),
    G_VARIANT_TYPE("(a{sv})"),
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    &error
  );
  // clang-format on

  if (reply != NULL) {
    GVariant* properties = g_variant_get_child_value(reply, 0);
    _linux_parse_properties(&player, properties);
    g_variant_unref(properties);
    g_variant_unref(reply);
  } else {
    g_clear_error(&error);
  }

  // clang-format off
  platform_data->signal_subscription_id = g_dbus_connection_signal_subscribe(
    connection,
    player_name,
    "org.freedesktop.DBus.Properties",
    "PropertiesChanged",
    "/org/mpris/MediaPlayer2",
    NULL,
    G_DBUS_SIGNAL_FLAGS_NONE,
    _linux_on_properties_changed,
    g_strdup(player_name),
    g_free
  );
  // clang-format on

  __wnp_update_player(&player);
  __wnp_end_update_cycle();
}

static void _linux_player_removed(GDBusConnection* connection, const gchar* player_name) {
  wnp_player_t players[WNP_MAX_PLAYERS] = {0};
  int count = __wnp_start_update_cycle(players);

  for (size_t i = 0; i < count; i++) {
    _linux_platform_data_t* platform_data = _linux_get_platform_data(&players[i]);
    if (platform_data != NULL && g_strcmp0(platform_data->player_name, player_name) == 0) {
      __wnp_remove_player(players[i].id);
    }
  }

  __wnp_end_update_cycle();
}
// clang-format off

static void _linux_on_name_owner_changed(
  GDBusConnection* connection, 
  const gchar* sender_name,
  const gchar* object_path,
  const gchar* interface_name,
  const gchar* signal_name,
  GVariant* parameters,
  gpointer user_data
) {
  // clang-format on
  const gchar* name;
  const gchar* old_owner;
  const gchar* new_owner;

  g_variant_get(parameters, "(&s&s&s)", &name, &old_owner, &new_owner);

  if (g_str_has_prefix(name, "org.mpris.MediaPlayer2")) {
    if (new_owner && *new_owner != '\0') {
      _linux_player_added(connection, name);
    } else if (old_owner && *old_owner != '\0') {
      _linux_player_removed(connection, name);
    }
  }
}

static void _linux_init_active_players(GDBusConnection* connection) {
  GError* error = NULL;
  GVariant* result;
  gchar** names;
  gint i;

  // clang-format off
  result = g_dbus_connection_call_sync(
    connection,
    "org.freedesktop.DBus",
    "/org/freedesktop/Dbus",
    "org.freedesktop.DBus",
    "ListNames",
    NULL,
    G_VARIANT_TYPE("(as)"),
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    &error);
  // clang-format on

  if (error != NULL) {
    g_clear_error(&error);
    return;
  }

  g_variant_get(result, "(^as)", &names);

  for (i = 0; names[i] != NULL; i++) {
    if (g_str_has_prefix(names[i], "org.mpris.MediaPlayer2")) {
      _linux_player_added(connection, names[i]);
    }
  }

  g_strfreev(names);
  g_variant_unref(result);
}

static int _wnp_loop_thread_func(void* user_data) {
  g_main_loop_run(_linux_state.loop);
  return 0;
}

/**
 * =============================
 * | Shared internal functions |
 * =============================
 */

wnp_init_ret_t __wnp_platform_linux_init() {
  GDBusProxy* proxy;
  GError* error = NULL;

  _linux_state.connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
  if (error) {
    g_clear_error(&error);
    return WNP_INIT_LINUX_DBUS_ERROR;
  }

  _linux_init_active_players(_linux_state.connection);

  // clang-format off
  _linux_state.signal_subscription_id = g_dbus_connection_signal_subscribe(
    _linux_state.connection,
    "org.freedesktop.DBus",
    "org.freedesktop.DBus",
    "NameOwnerChanged",
    "/org/freedesktop/DBus",
    NULL,
    G_DBUS_SIGNAL_FLAGS_NONE,
    _linux_on_name_owner_changed,
    NULL,
    NULL
  );
  // clang-format on

  _linux_state.loop = g_main_loop_new(NULL, FALSE);
  _linux_state.loop_thread = thread_create(_wnp_loop_thread_func, NULL, THREAD_STACK_SIZE_DEFAULT);

  return WNP_INIT_SUCCESS;
}

void __wnp_platform_linux_uninit() {
  g_dbus_connection_signal_unsubscribe(_linux_state.connection, _linux_state.signal_subscription_id);
  g_main_loop_quit(_linux_state.loop);
  g_main_loop_unref(_linux_state.loop);
  g_object_unref(_linux_state.connection);
  thread_join(_linux_state.loop_thread);
  thread_destroy(_linux_state.loop_thread);
}

void __wnp_platform_linux_free(void* _platform_data) {
  _linux_platform_data_t* platform_data = (_linux_platform_data_t*)_platform_data;

  if (platform_data != NULL) {
    g_dbus_connection_signal_unsubscribe(_linux_state.connection, platform_data->signal_subscription_id);
    free(platform_data->player_name);
    free(platform_data);
  }
}

void __wnp_platform_linux_event(wnp_player_t* player, wnp_event_t event, int event_id, int data) {
  _linux_platform_data_t* platform_data = _linux_get_platform_data(player);
  if (platform_data == NULL) return;

  GError* error = NULL;
  gchar method_name[WNP_STR_LEN] = {0};
  GVariant* parameters = NULL;

  switch (event) {
    case WNP_TRY_SET_STATE: {
      switch ((wnp_state_t)data) {
        case WNP_STATE_PLAYING: {
          strncpy(method_name, "Play", WNP_STR_LEN);
          break;
        }
        case WNP_STATE_PAUSED: {
          strncpy(method_name, "Pause", WNP_STR_LEN);
          break;
        }
        case WNP_STATE_STOPPED: {
          strncpy(method_name, "Stop", WNP_STR_LEN);
          break;
        }
      }
      break;
    }
    case WNP_TRY_SKIP_PREVIOUS: {
      strncpy(method_name, "Previous", WNP_STR_LEN);
      break;
    }
    case WNP_TRY_SKIP_NEXT: {
      strncpy(method_name, "Next", WNP_STR_LEN);
      break;
    }
    case WNP_TRY_SET_POSITION: {
      gint64 new_position = data * 1000000;
      strncpy(method_name, "SetPosition", WNP_STR_LEN);
      parameters = g_variant_new("(ox)", "/org/mpris/MediaPlayer2", new_position);
      break;
    }
    case WNP_TRY_SET_VOLUME: {
      strncpy(method_name, "Set", WNP_STR_LEN);
      parameters = g_variant_new("(ssv)", "/org/mpris/MediaPlayer2", "Volume", g_variant_new_double((gdouble)data / 100));
      break;
    }
    case WNP_TRY_SET_RATING: {
      break;
    }
    case WNP_TRY_SET_REPEAT: {
      const gchar* repeat_mode = (data == WNP_REPEAT_ONE) ? "Track" : (data == WNP_REPEAT_ALL) ? "Playlist" : "None";
      strncpy(method_name, "Set", WNP_STR_LEN);
      parameters = g_variant_new("(ssv)", "/org/mpris/MediaPlayer2", "LoopStatus", g_variant_new_string(repeat_mode));
      break;
    }
    case WNP_TRY_SET_SHUFFLE: {
      gboolean shuffle = (data == true);
      strncpy(method_name, "Set", WNP_STR_LEN);
      parameters = g_variant_new("(ssv)", "/org/mpris/MediaPlayer2", "Shuffle", g_variant_new_boolean(shuffle));
      break;
    }
  }

  // clang-format off
  g_dbus_connection_call_sync(
    _linux_state.connection,
    platform_data->player_name,
    "/org/mpris/MediaPlayer2",
    "org.mpris.MediaPlayer2.Player",
    method_name,
    parameters,
    NULL,
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    &error
  );
  // clang-format on

  if (error == NULL) {
    __wnp_set_event_result(event_id, WNP_EVENT_SUCCEEDED);
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
  } else {
    __wnp_set_event_result(event_id, WNP_EVENT_FAILED);
    g_clear_error(&error);
  }
}

#endif /* WNP_BUILD_PLATFORM_LINUX */
