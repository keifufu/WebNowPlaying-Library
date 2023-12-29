#include "../src/cws.h"
#include <stdio.h>
#include <string.h>

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

void ws_on_open(cws_client_t* client)
{
}

void ws_on_close(cws_client_t* client)
{
}

void ws_on_message(cws_client_t* client, const unsigned char* msg, uint64_t size, int type)
{
  cws_send(client, (char*)msg, size, type);
}

int main()
{
  int ret = cws_start((cws_server_t){
      .port = 1234,
      .on_open = &ws_on_open,
      .on_close = &ws_on_close,
      .on_message = &ws_on_message,
  });

  if (ret != 0) {
    printf("Failed to start websocket server: %d\n", ret);
    return 1;
  }

  sleep_ms(30 * 1000);
  cws_stop();
  return 0;
}