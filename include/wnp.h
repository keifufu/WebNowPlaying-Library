#ifndef WNP_H
#define WNP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WNP_MAX_PLAYERS 64
#define WNP_MAX_EVENT_RESULTS 512
#define WNP_STR_LEN 512

typedef enum {
  WNP_STATE_PLAYING = 0,
  WNP_STATE_PAUSED = 1,
  WNP_STATE_STOPPED = 2,
} wnp_state_t;

typedef enum {
  WNP_REPEAT_NONE = (1 << 0),
  WNP_REPEAT_ALL = (1 << 1),
  WNP_REPEAT_ONE = (1 << 2),
} wnp_repeat_t;

typedef enum {
  WNP_RATING_SYSTEM_NONE = 0,
  WNP_RATING_SYSTEM_LIKE = 1,
  WNP_RATING_SYSTEM_LIKE_DISLIKE = 2,
  WNP_RATING_SYSTEM_SCALE = 3,
} wnp_rating_system_t;

typedef enum {
  WNP_PLATFORM_NONE = 0, // used by WNP_DEFAULT_PLAYER
  WNP_PLATFORM_WEB = 1,
  WNP_PLATFORM_LINUX = 2,
  WNP_PLATFORM_DARWIN = 3,
  WNP_PLATFORM_WINDOWS = 4,
} wnp_platform_t;

/**
 * Information about a player.
 *
 * Actions on the player are not meant to be set here,
 * but via wnp_try... functions.
 *
 * All strings are UTF-8, if you need UTF-16 use
 * `wnp_utf8_to_utf16`
 */

typedef struct {
  /* The players id. Can be -1 if `WNP_DEFAULT_PLAYER`. Otherwise ranging from 0 to `WNP_MAX_PLAYERS - 1` */
  int id;
  /**
   * The players name.
   * Eg: YouTube, Spotify...
   * If using desktop players this can be an
   * executables name (firefox.exe) or a windows appid.
   */
  char name[WNP_STR_LEN];
  /* The title. Can be empty. */
  char title[WNP_STR_LEN];
  /* The artist. Can be empty. */
  char artist[WNP_STR_LEN];
  /* The album. Can be empty. */
  char album[WNP_STR_LEN];
  /**
   * The path to the cover of the playing media.
   * Can be an empty string if no cover exists.
   * If not empty, this is always a png, starting with file://
   */
  char cover[WNP_STR_LEN];
  /**
   * The cover source of the playing media.
   * Can be:
   * - http(s)://...
   * - file://...
   * - data:image... (only type, no data)
   * - empty
   */
  char cover_src[WNP_STR_LEN];
  /* The state of the player */
  wnp_state_t state;
  /* The position in seconds */
  unsigned int position;
  /* The duration in seconds */
  unsigned int duration;
  /* The volume from 0 to 100 */
  unsigned int volume;
  /**
   * The rating from 0 to 5.
   * Depending on the `rating_system` this
   * can mean multiple things.
   *
   * `WNP_RATING_SCALE`:
   * This is used for sites that have
   * a star-based rating system.
   *
   * `WNP_RATING_LIKE`, `WNP_RATING_LIKE_DISLIKE`:
   * 0 - unrated
   * 1 - disliked
   * 5 - liked
   */
  int rating;
  /* The repeat mode of the player */
  wnp_repeat_t repeat;
  /* Whether shuffle is enabled */
  bool shuffle;
  /**
   * The rating system.
   * More info above, in `rating`.
   */
  wnp_rating_system_t rating_system;
  /**
   * Available repeat modes.
   * This is a flag of `wnp_repeat`.
   *
   * Checking if repeat mode "ALL" is supported:
   * `player->available_repeat & WNP_REPEAT_ALL`
   */
  unsigned int available_repeat;
  /* If the state can currently be set */
  bool can_set_state;
  /* If skipping back is currently possible */
  bool can_skip_previous;
  /* If skipping next is currently possible */
  bool can_skip_next;
  /* If the position can currently be set */
  bool can_set_position;
  /* If the volume can currently be set */
  bool can_set_volume;
  /* If the rating can currently be set */
  bool can_set_rating;
  /* If the repeat mode can currently be set */
  bool can_set_repeat;
  /* If shuffle can currently be set */
  bool can_set_shuffle;
  /**
   * Timestamps in milliseconds since unix epoch (utc).
   * `created_at` - Player created
   * `updated_at` - Player updated
   * `active_at` - used in calculation for `wnp_get_active_player`
   */
  uint64_t created_at;
  uint64_t updated_at;
  uint64_t active_at;
  bool is_web_browser;
  /* The platform a player is from */
  wnp_platform_t platform;
  /* Internal data, do not use. */
  void* _platform_data;
} wnp_player_t;

/**
 * Default player struct.
 * Instantiate your players using this instead of `{0}` or similar methods.
 */
extern wnp_player_t WNP_DEFAULT_PLAYER;

/* args for `wnp_init` */
typedef struct {
  /* Port number for the WEB platform. Set to 0 to disable WEB. */
  int web_port;
  /* Adapter version (semver) */
  char adapter_version[WNP_STR_LEN];
  // Callback invoked after a player is added
  void (*on_player_added)(wnp_player_t* player, void* data);
  // Callback invoked after a player is updated
  void (*on_player_updated)(wnp_player_t* player, void* data);
  // Callback invoked before a player is removed
  void (*on_player_removed)(wnp_player_t* player, void* data);
  // Callback invoked after the active player changed; `player` can be NULL if no active player is found
  void (*on_active_player_changed)(wnp_player_t* player, void* data);
  // Additional data to be passed to callback functions
  void* callback_data;
} wnp_args_t;

/* Return values for `wnp_init` */
typedef enum {
  WNP_INIT_SUCCESS = 0,
  WNP_INIT_ALREADY_INITIALIZED = 1,
  WNP_INIT_WEB_PORT_IN_USE = 2,
  WNP_INIT_LINUX_DBUS_ERROR = 3,
  WNP_INIT_DARWIN_FAILED = 4,
} wnp_init_ret_t;

/* Initializes and starts WebNowPlaying. */
wnp_init_ret_t wnp_init(wnp_args_t* args);

/* Uninitializes and stops WebNowPlaying. */
void wnp_uninit();

/* Whether WebNowPlaying is initialized and started or not. */
bool wnp_is_initialized();

/**
 * Attempts to copy the player with the given id into `player_out`.
 * Returns `true` if the player was found and copied, `false` otherwise.
 */
bool wnp_get_player(int player_id, wnp_player_t* player_out);

/**
 * Attempts to copy the active player into `player_out`.
 * Returns `true` if an active player was found and copied, `false` otherwise.
 *
 * Finds either:
 * - A player that is playing, not muted, with the most recent `active_at` timestamp.
 * - The player with the most recent `active_at` timestamp.
 */
bool wnp_get_active_player(wnp_player_t* player_out);

/**
 * Copies all players into `players_out` and returns the amount of players copied.
 * If `NULL` is passed as argument, only the number of players is returned.
 */
int wnp_get_all_players(wnp_player_t players_out[WNP_MAX_PLAYERS]);

/* Gets the current position in percent from 0.0f to 100.0f */
float wnp_get_position_percent(wnp_player_t* player);

/* Gets the remaining seconds. */
int wnp_get_remaining_seconds(wnp_player_t* player);

/**
 * Formats seconds to a string.
 *
 * pad_with_zeroes = false:
 *  - 30 = "0:30"
 *  - 69 = "1:09"
 *  - 6969 = "1:59:09"
 *
 * pad_with_zeroes = true
 *  - 30 = "00:30"
 *  - 69 = "01:09"
 *  - 6969 = "01:56:09"
 */
void wnp_format_seconds(int seconds, bool pad_with_zeroes, char str_out[13]);

/**
 * Converts UTF-8 to UTF-16
 */
void wnp_utf8_to_utf16(unsigned char* input, int input_len, uint16_t* output, int output_len);

typedef enum {
  WNP_EVENT_PENDING = 0,
  WNP_EVENT_SUCCEEDED = 1,
  WNP_EVENT_FAILED = 2,
} wnp_event_result_t;

/**
 * Gets the result for an event.
 *
 * event_id - The id returned from an event function
 */
wnp_event_result_t wnp_get_event_result(int event_id);

/**
 * Block until an event result is recieved.
 *
 * This checks if the event is still pending every 10ms.
 * Depending on the platform this is either near-instant or
 * can be up to ~300ms.
 *
 * If still pending after 1s it returns WNP_EVENT_FAILED.
 *
 * event_id - The id returned from an event function
 */
wnp_event_result_t wnp_wait_for_event_result(int event_id);

/* Base event functions */

int wnp_try_set_state(wnp_player_t* player, wnp_state_t state);
int wnp_try_skip_previous(wnp_player_t* player);
int wnp_try_skip_next(wnp_player_t* player);
int wnp_try_set_position(wnp_player_t* player, unsigned int seconds);
int wnp_try_set_volume(wnp_player_t* player, unsigned int volume);
int wnp_try_set_rating(wnp_player_t* player, unsigned int rating);
int wnp_try_set_repeat(wnp_player_t* player, wnp_repeat_t repeat);
int wnp_try_set_shuffle(wnp_player_t* player, bool shuffle);

/* Util event functions */

int wnp_try_play_pause(wnp_player_t* player);
int wnp_try_revert(wnp_player_t* player, unsigned int seconds);
int wnp_try_forward(wnp_player_t* player, unsigned int seconds);
int wnp_try_set_position_percent(wnp_player_t* player, float percent);
int wnp_try_revert_percent(wnp_player_t* player, float percent);
int wnp_try_forward_percent(wnp_player_t* player, float percent);
int wnp_try_toggle_repeat(wnp_player_t* player);

#ifdef __cplusplus
}
#endif

#endif /* WNP_H */
