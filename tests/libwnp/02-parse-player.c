#include "test.h"
#include "wnp.h"

void normal_data()
{
  struct wnp_player* player = wnp_create_player();
  assert(player != NULL);
  struct wnp_conn_data* conn_data = (struct wnp_conn_data*)calloc(1, sizeof(struct wnp_conn_data));
  struct wnp_player_data* player_data = player->_data;
  player_data->conn_data = conn_data;

  // Album should be set to empty because data contains \1 as album
  wnp_assign_str(player->album, "xd");
  // Artist should not be overwritten because data does not contain artist
  wnp_assign_str(player->artist, "Artist");

  char* data =
      strdup("8|PlayerName|Title with \\| pipe||\1|https://some.url/|1|5|267|3|5|2|1|1|7|1|1|1|1|1|1|1|1|1702093090496|1702093090497|1702093090497|");
  wnp_parse_player_text(player, data);

  assert(((struct wnp_player_data*)player->_data)->conn_data->id == 8);
  assert(strcmp(player->name, "PlayerName") == 0);
  assert(strcmp(player->title, "Title with | pipe") == 0);
  assert(strcmp(player->artist, "Artist") == 0);
  assert(strcmp(player->album, "") == 0);
  assert(strcmp(player->cover_src, "https://some.url/") == 0);
  assert(player->state == WNP_STATE_PAUSED);
  assert(player->position == 5);
  assert(player->duration == 267);
  assert(player->volume == 3);
  assert(player->rating == 5);
  assert(player->repeat == WNP_REPEAT_ALL);
  assert(player->shuffle == true);
  assert(player->rating_system == WNP_RATING_SYSTEM_LIKE);
  assert(player->available_repeat == (WNP_REPEAT_NONE | WNP_REPEAT_ALL | WNP_REPEAT_ONE));
  assert(player->can_set_state == true);
  assert(player->can_skip_previous == true);
  assert(player->can_skip_next == true);
  assert(player->can_set_position == true);
  assert(player->can_set_volume == true);
  assert(player->can_set_rating == true);
  assert(player->can_set_repeat == true);
  assert(player->can_set_shuffle == true);
  assert(player->created_at == 1702093090496);
  assert(player->updated_at == 1702093090497);
  assert(player->active_at == 1702093090497);
}

void invalid_data()
{
  struct wnp_player* player = wnp_create_player();
  assert(player != NULL);
  struct wnp_conn_data* conn_data = (struct wnp_conn_data*)calloc(1, sizeof(struct wnp_conn_data));
  struct wnp_player_data* player_data = player->_data;
  player_data->conn_data = conn_data;
  // Player will be filled with garbage data, but the program does not segfault.
  char* data = strdup("09awd|some|major|fucking|garbage234#34r0ß9aw r8f||||||\\|||||||||||||||||||||||||||||||||||||0");
  wnp_parse_player_text(player, data);
}

int main()
{
  wnp_init_globals(true);
  normal_data();
  invalid_data();
  return 0;
}