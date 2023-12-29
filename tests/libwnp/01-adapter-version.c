#include "test.h"

int main()
{

  assert(wnp_is_valid_adapter_version("a1.0.0") == false);
  assert(wnp_is_valid_adapter_version("1.a.0.1") == false);
  assert(wnp_is_valid_adapter_version("1.0.0.0") == false);
  assert(wnp_is_valid_adapter_version("1..01") == false);
  assert(wnp_is_valid_adapter_version("1.0.0") == true);

  return 0;
}