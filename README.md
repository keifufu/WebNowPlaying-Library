# WebNowPlaying-Library

C library for [WebNowPlaying](https://github.com/keifufu/WebNowPlaying) adapters.

## Usage

- See [wnp.h](https://github.com/keifufu/WebNowPlaying-Library/blob/main/include/wnp.h)
- See examples in [/examples](https://github.com/keifufu/WebNowPlaying-Library/blob/main/examples)

```c
examples/simple.c:
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
    // get the "active" player
    wnp_player_t player = WNP_DEFAULT_PLAYER;
    wnp_get_active_player(&player);
    // print the title
    printf("Title: %s\n", player.title);
    // try to play-pause
    int event_id = wnp_try_play_pause(&player);
    // optionally, wait for a result
    char* event_outcomes[] = {"", "succeeded", "failed"};
    printf("event %d %s\n", event_id, event_outcomes[wnp_wait_for_event_result(event_id)]);
    sleep_ms(1000);
  }

  wnp_uninit();
  return EXIT_SUCCESS;
}

```
