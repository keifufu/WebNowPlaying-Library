#include "wnp.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

void sleep_ms(int ms)
{
#ifdef _WIN32
  Sleep(ms);
#else
  usleep(ms * 1000);
#endif
}

void on_player_added(struct wnp_player* player, void* data)
{
  wnp_lock(player);
  printf("Added player: %s\n", player->name);
  wnp_unlock(player);
}

void on_player_updated(struct wnp_player* player, void* data)
{
  wnp_lock(player);
  printf("Updated player: %s\n", player->name);
  wnp_unlock(player);
}

void on_player_removed(struct wnp_player* player, void* data)
{
  wnp_lock(player);
  printf("Removed player: %s\n", player->name);
  wnp_unlock(player);
}

int main(void)
{
  struct wnp_events events;
  events.on_player_added = &on_player_added;
  events.on_player_updated = &on_player_updated;
  events.on_player_removed = &on_player_removed;
  events.on_active_player_changed = NULL; // set NULL if no handler
  events.data = NULL;                     // additional data to be passed to callbacks

  // port, adapter version
  if (wnp_start(1234, "1.0.0", &events) != 0) {
    perror("Failed to start wnp");
    return EXIT_FAILURE;
  }

  sleep_ms(60000);

  wnp_stop();
  return EXIT_SUCCESS;
}