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

int main(void)
{
  // port, adapter version
  if (wnp_start(1234, "1.0.0", NULL) != 0) {
    perror("Failed to start wnp");
    return EXIT_FAILURE;
  }

  for (int i = 0; i < 60; i++) {
    // get the "active" player
    struct wnp_player* player = wnp_get_active_player(true);
    // print the title
    wnp_lock(player);
    printf("Title: %s\n", player->title);
    wnp_unlock(player);
    // try to play-pause
    int event_id = wnp_try_play_pause(player);
    // optionally, wait for a result
    char* event_outcomes[] = {"", "succeeded", "failed"};
    printf("event %d %s\n", event_id, event_outcomes[wnp_wait_for_event_result(event_id)]);
    sleep_ms(1000);
  }

  wnp_stop();
  return EXIT_SUCCESS;
}
```

## Language Bindings

- None for now

## Building

### Linux

Dependencies: `gnumake, clang`

- Run `make linux64`

### Windows

Dependencies: `gnumake, Visual Studio + MSVC toolchain`

- Run `./build-msvc.ps1`

Note: `build-mvsc.ps1` assumes you have Visual Studio 2022 installed at the default location to locate vcvars32.bat and vcvars64.bat, if you don't then simply replace the path.
