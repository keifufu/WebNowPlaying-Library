#include "test.h"
#include "wnp.h"

void test_format_seconds(int seconds, bool pad, char* cmp)
{
  char time_str[10];
  wnp_format_seconds(seconds, pad, time_str);
  assert(strcmp(time_str, cmp) == 0);
}

int main()
{
  wnp_init_globals(true);

  assert(wnp_get_event_result(0) == WNP_EVENT_PENDING);
  assert(wnp_get_event_result(WNP_MAX_EVENT_RESULTS) == WNP_EVENT_FAILED);
  assert(wnp_wait_for_event_result(WNP_MAX_EVENT_RESULTS) == WNP_EVENT_FAILED);
  assert(wnp_wait_for_event_result(0) == WNP_EVENT_FAILED);

  struct wnp_player* player = wnp_create_player();
  assert(player != NULL);
  player->duration = 100;
  player->position = 10;

  assert(wnp_get_position_percent(player) == 10.0f);
  assert(wnp_get_remaining_seconds(player) == 90);

  test_format_seconds(500, false, "8:20");
  test_format_seconds(500, true, "08:20");
  test_format_seconds(10, false, "0:10");
  test_format_seconds(10, true, "00:10");
  test_format_seconds(30000, false, "8:20:00");
  test_format_seconds(30000, true, "08:20:00");

  return 0;
}