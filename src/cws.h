#ifndef CWS_H
#define CWS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CWS_TYPE_TEXT 1
#define CWS_TYPE_BINARY 2

typedef struct cws_client cws_client_t;

typedef struct {
  uint16_t port;
  void (*on_open)(cws_client_t* client);
  void (*on_close)(cws_client_t* client);
  void (*on_message)(cws_client_t* client, const unsigned char* msg, uint64_t msg_size, int type);
} cws_server_t;

extern int cws_start(cws_server_t server);
extern int cws_stop();
extern int cws_send(cws_client_t* client, const char* msg, uint64_t size, int type);

#ifdef __cplusplus
}
#endif

#endif /* CWS_H */
