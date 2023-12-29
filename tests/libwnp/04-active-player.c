#include "test.h"
#include "wnp.h"

void set_players()
{
  struct wnp_player* player0 = wnp_create_player();
  assert(player0 != NULL);
  player0->volume = 100;
  player0->state = WNP_STATE_PLAYING;
  player0->active_at = 1;

  struct wnp_player* player1 = wnp_create_player();
  assert(player1 != NULL);
  player1->volume = 100;
  player1->state = WNP_STATE_STOPPED;
  player1->active_at = 2;

  struct wnp_player* player2 = wnp_create_player();
  assert(player2 != NULL);
  player2->volume = 0;
  player2->state = WNP_STATE_PLAYING;
  player2->active_at = 3;
}

int main()
{
  wnp_init_globals(true);
  set_players();

  wnp_recalculate_active_player();

  struct wnp_player* player = wnp_get_active_player(false);
  assert(player != NULL);
  assert(player->id == 0);

  for (size_t i = 0; i < WNP_MAX_PLAYERS; i++) {
    wnp_free_player(&g_wnp_players[i]);
  }
  assert(wnp_get_active_player(false) == NULL);
  assert(wnp_get_active_player(true)->id == -1);

  return 0;
}