#include "sleep.h"
#include "wnp.h"
#include <stdio.h>
#include <stdlib.h>

int main()
{
  wnp_args_t args = {
      .web_port = 1234,
      .adapter_version = "1.0.0",
      .on_player_added = NULL,
      .on_player_updated = NULL,
      .on_player_removed = NULL,
      .on_active_player_changed = NULL,
      .callback_data = NULL,
  };

  if (wnp_init(&args) != WNP_INIT_SUCCESS) {
    fprintf(stderr, "Failed to initialize WebNowPlaying");
    exit(EXIT_FAILURE);
  }

  for (size_t i = 0; i < 60; i++) {
    // get all players and print the title of each
    wnp_player_t players[WNP_MAX_PLAYERS] = {0};
    int count = wnp_get_all_players(players);

    for (size_t i = 0; i < count; i++) {
      printf("Title: %s\n", players[i].title);
    }

    sleep_ms(1000);
  }

  wnp_uninit();
  return EXIT_SUCCESS;
}
