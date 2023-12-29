#include "test.h"

int main()
{
  char* path = wnp_nodp_filepath();

  wnp_set_use_dp(false);
  assert(access(path, F_OK) == 0);
  assert(wnp_read_use_dp() == false);

  wnp_set_use_dp(true);
  assert(access(path, F_OK) != 0);
  assert(wnp_read_use_dp() == true);

  return 0;
}