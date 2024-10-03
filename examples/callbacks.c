#include "wnp.h"
#include <stdio.h>
#include <stdlib.h>

void on_player_added(wnp_player_t* player, void* callback_data) {
  printf("Added player: %s\n", player->name);
}

void on_player_updated(wnp_player_t* player, void* callback_data) {
  printf("Updated player: %s\n", player->name);
}

void on_player_removed(wnp_player_t* player, void* callback_data) {
  printf("Removed player: %s\n", player->name);
}

void on_active_player_changed(wnp_player_t* player, void* callback_data) {
  if (player == NULL) {
    printf("Active player changed to: None\n");
  } else {
    printf("Active player changed to: %s\n", player->name);
  }
}

int main() {
  wnp_args_t args = {
      .web_port = 1234,
      .adapter_version = "1.0.0",
      .on_player_added = &on_player_added,
      .on_player_updated = &on_player_updated,
      .on_player_removed = &on_player_removed,
      .on_active_player_changed = &on_active_player_changed,
      .callback_data = NULL,
  };

  if (wnp_init(&args) != WNP_INIT_SUCCESS) {
    fprintf(stderr, "Failed to initialize WebNowPlaying");
    exit(EXIT_FAILURE);
  }

  getchar();

  wnp_uninit();
  return EXIT_SUCCESS;
}
