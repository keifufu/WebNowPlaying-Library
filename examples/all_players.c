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

int main(void)
{
  // port, adapter version
  if (wnp_start(1234, "1.0.0", NULL) != 0) {
    perror("Failed to start wnp");
    return EXIT_FAILURE;
  }

  for (int i = 0; i < 60; i++) {
    // get all players and print the title of each
    struct wnp_player* players[WNP_MAX_PLAYERS];
    int num = wnp_get_all_players(players);

    for (int i = 0; i < num; i++) {
      wnp_lock(players[i]);
      printf("Title: %s\n", players[i]->title);
      wnp_unlock(players[i]);
    }

    sleep_ms(1000);
  }

  wnp_stop();
  return EXIT_SUCCESS;
}