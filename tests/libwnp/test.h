#include "../src/thread.h"
#include "wnp.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

bool wnp_is_valid_adapter_version(const char* adapter_version);
struct wnp_player* wnp_create_player();
void wnp_free_player(struct wnp_player* player);
void wnp_assign_str(char dest[WNP_STR_LEN], char* str);
void wnp_parse_player_text(struct wnp_player* player, char* data);
struct wnp_conn_data {
  void* client;
  int id;
};
struct wnp_player_data {
  thread_mutex_t lock;
  struct wnp_conn_data* conn_data;
  void* dp_data;
};
void wnp_init_globals(bool start);
char* wnp_nodp_filepath();
void wnp_set_use_dp(int use_desktop_players);
bool wnp_read_use_dp();
void wnp_recalculate_active_player();
extern struct wnp_player g_wnp_players[WNP_MAX_PLAYERS];