#include "wnp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

wnp_player_t players[WNP_MAX_PLAYERS] = {0};

void on_player_added(wnp_player_t* player, void* callback_data) {
  players[player->id] = *player;
  printf("=== PLAYER ADDED ===\n");
  printf("id:                 %d\n", player->id);
  printf("name:               %s\n", player->name);
  printf("title:              %s\n", player->title);
  printf("artist:             %s\n", player->artist);
  printf("album:              %s\n", player->album);
  printf("cover:              %s\n", player->cover);
  printf("cover_src:          %s\n", player->cover_src);
  printf("state:              %d\n", player->state);
  printf("position:           %d\n", player->position);
  printf("duration:           %d\n", player->duration);
  printf("volume:             %d\n", player->volume);
  printf("rating:             %d\n", player->rating);
  printf("repeat:             %d\n", player->repeat);
  printf("shuffle:            %d\n", player->shuffle);
  printf("rating_system:      %d\n", player->rating_system);
  printf("available_repeat:   %d\n", player->available_repeat);
  printf("can_set_state       %d\n", player->can_set_state);
  printf("can_skip_previous   %d\n", player->can_skip_previous);
  printf("can_skip_next       %d\n", player->can_skip_next);
  printf("can_set_position    %d\n", player->can_set_position);
  printf("can_set_volume      %d\n", player->can_set_volume);
  printf("can_set_rating      %d\n", player->can_set_rating);
  printf("can_set_repeat      %d\n", player->can_set_repeat);
  printf("can_set_shuffle     %d\n", player->can_set_shuffle);
  printf("created_at          %ld\n", player->created_at);
  printf("updated_at          %ld\n", player->updated_at);
  printf("active_at           %ld\n", player->active_at);
  printf("platform            %d\n", player->platform);
  printf("====================\n");
}

void on_player_updated(wnp_player_t* player, void* callback_data) {
  printf("=== PLAYER UPDATED ===\n");
  printf("%s (%d)\n", player->name, player->id);
  printf("=== CHANGED VALUES ===\n");
  if (strcmp(players[player->id].title, player->title) != 0) printf("title:              %s\n", player->title);
  if (strcmp(players[player->id].artist, player->artist) != 0) printf("artist:             %s\n", player->artist);
  if (strcmp(players[player->id].album, player->album) != 0) printf("album:              %s\n", player->album);
  if (strcmp(players[player->id].cover, player->cover) != 0) printf("cover:              %s\n", player->cover);
  if (strcmp(players[player->id].cover_src, player->cover_src) != 0) printf("cover_src:          %s\n", player->cover_src);
  if (players[player->id].state != player->state) printf("state:              %d\n", player->state);
  if (players[player->id].position != player->position) printf("position:           %d\n", player->position);
  if (players[player->id].duration != player->duration) printf("duration:           %d\n", player->duration);
  if (players[player->id].volume != player->volume) printf("volume:             %d\n", player->volume);
  if (players[player->id].rating != player->rating) printf("rating:             %d\n", player->rating);
  if (players[player->id].repeat != player->repeat) printf("repeat:             %d\n", player->repeat);
  if (players[player->id].shuffle != player->shuffle) printf("shuffle:            %d\n", player->shuffle);
  if (players[player->id].rating_system != player->rating_system) printf("rating_system:      %d\n", player->rating_system);
  if (players[player->id].available_repeat != player->available_repeat) printf("available_repeat:   %d\n", player->available_repeat);
  if (players[player->id].can_set_state != player->can_set_state) printf("can_set_state       %d\n", player->can_set_state);
  if (players[player->id].can_skip_previous != player->can_skip_previous) printf("can_skip_previous   %d\n", player->can_skip_previous);
  if (players[player->id].can_skip_next != player->can_skip_next) printf("can_skip_next       %d\n", player->can_skip_next);
  if (players[player->id].can_set_position != player->can_set_position) printf("can_set_position    %d\n", player->can_set_position);
  if (players[player->id].can_set_volume != player->can_set_volume) printf("can_set_volume      %d\n", player->can_set_volume);
  if (players[player->id].can_set_rating != player->can_set_rating) printf("can_set_rating      %d\n", player->can_set_rating);
  if (players[player->id].can_set_repeat != player->can_set_repeat) printf("can_set_repeat      %d\n", player->can_set_repeat);
  if (players[player->id].can_set_shuffle != player->can_set_shuffle) printf("can_set_shuffle     %d\n", player->can_set_shuffle);
  if (players[player->id].created_at != player->created_at) printf("created_at          %ld\n", player->created_at);
  if (players[player->id].updated_at != player->updated_at) printf("updated_at          %ld\n", player->updated_at);
  if (players[player->id].active_at != player->active_at) printf("active_at           %ld\n", player->active_at);
  if (players[player->id].platform != player->platform) printf("platform            %d\n", player->platform);
  printf("======================\n");
  players[player->id] = *player;
}

void on_player_removed(wnp_player_t* player, void* callback_data) {
  printf("Player removed: %s (%d)\n", player->name, player->id);
  players[player->id] = WNP_DEFAULT_PLAYER;
}

void on_active_player_changed(wnp_player_t* player, void* callback_data) {
  if (player == NULL) {
    printf("Active player changed to: None\n");
  } else {
    printf("Active player changed to: %s\n", player->name);
  }
}

int main() {
  for (size_t i = 0; i < WNP_MAX_PLAYERS; i++) {
    players[i] = WNP_DEFAULT_PLAYER;
  }

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
