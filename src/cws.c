#define THREAD_IMPLEMENTATION
#include "thread.h"

#include "cws.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#define MSG_NOSIGNAL 0
#define ssize_t SSIZE_T
#define strtok_r strtok_s
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <unistd.h>
#endif

#define MAX_CLIENTS 64

#define STATE_CONNECTING 0
#define STATE_OPEN 1
#define STATE_CLOSING 2
#define STATE_CLOSED 3

#define OP_CONTINUATION 0
#define OP_TEXT 1
#define OP_BINARY 2
#define OP_CLOSE 8
#define OP_PING 0x9
#define OP_PONG 0xA

struct cws_frame {
  cws_client_t* client;
  unsigned char buf[2048];
  unsigned char* msg;
  unsigned char msg_ctrl[125];
  uint64_t frame_size;
  size_t amt_read;
  int op;
  size_t cur_pos;
  int error;
};

struct cws_frame_state_data {
  unsigned char* msg_data;
  unsigned char* msg_ctrl;
  uint8_t masks_data[4];
  uint8_t masks_ctrl[4];
  uint64_t msg_data_index;
  uint64_t msg_ctrl_index;
  uint64_t frame_length;
  uint64_t frame_size;
  int32_t pong_id;
  uint8_t opcode;
  uint8_t is_fin;
  uint8_t mask;
  int cur_byte;
  uint32_t utf8_state;
};

struct cws_client {
  thread_mutex_t lock;
  int fd;
  int state;
  cws_server_t* server;
  int32_t last_pong_id;
  int32_t current_ping_id;
};

struct cws_server_data {
  cws_server_t server;
  int fd;
};

static thread_mutex_t g_clients_mutex;
static cws_client_t g_clients[MAX_CLIENTS];
static thread_atomic_int_t g_exit_flag;
static struct cws_server_data g_server_data;

static unsigned char* base64_encode(const unsigned char* input, size_t length)
{
  static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  int output_length = 4 * ((length + 2) / 3);
  unsigned char* encoded_data = (unsigned char*)malloc(output_length + 1);

  if (encoded_data == NULL) {
    return NULL;
  }

  int i = 0, j = 0;
  while (i < length) {
    uint32_t octet_a = (i < length) ? input[i++] : 0;
    uint32_t octet_b = (i < length) ? input[i++] : 0;
    uint32_t octet_c = (i < length) ? input[i++] : 0;

    uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

    encoded_data[j++] = base64_chars[(triple >> 3 * 6) & 0x3F];
    encoded_data[j++] = base64_chars[(triple >> 2 * 6) & 0x3F];
    encoded_data[j++] = base64_chars[(triple >> 1 * 6) & 0x3F];
    encoded_data[j++] = base64_chars[(triple >> 0 * 6) & 0x3F];
  }

  for (size_t i = 0; i < (3 - length % 3) % 3; i++) {
    encoded_data[output_length - 1 - i] = '=';
  }

  encoded_data[j] = '\0';
  return encoded_data;
}

static void close_fd(int fd)
{
#ifdef _WIN32
  shutdown(fd, SD_BOTH);
  closesocket(fd);
#else
  shutdown(fd, SHUT_RDWR);
  close(fd);
#endif
}

static ssize_t sendn_client(cws_client_t* client, const void* buf, size_t n, int flags)
{
  if (client == NULL || client->fd == -1) {
    return -1;
  }

  ssize_t bytes = 0, ret = 0;

  thread_mutex_lock(&client->lock);
  while (bytes < n) {
    ret = send(client->fd, (char*)buf + bytes, n - bytes, flags);
    if (ret == -1) {
      thread_mutex_unlock(&client->lock);
      return -1;
    }
    bytes += ret;
  }

  thread_mutex_unlock(&client->lock);
  return bytes;
}

int cws_send(cws_client_t* client, const char* msg, uint64_t size, int type)
{
  unsigned char frame[10];
  uint8_t data_start;

  frame[0] = (128 | type);

  if (size <= 125) {
    frame[1] = size & 0x7F;
    data_start = 2;
  } else if (size >= 126 && size <= 65535) {
    frame[1] = 126;
    frame[2] = (size >> 8) & 255;
    frame[3] = size & 255;
    data_start = 4;
  } else {
    frame[1] = 127;
    frame[2] = (unsigned char)((size >> 56) & 255);
    frame[3] = (unsigned char)((size >> 48) & 255);
    frame[4] = (unsigned char)((size >> 40) & 255);
    frame[5] = (unsigned char)((size >> 32) & 255);
    frame[6] = (unsigned char)((size >> 24) & 255);
    frame[7] = (unsigned char)((size >> 16) & 255);
    frame[8] = (unsigned char)((size >> 8) & 255);
    frame[9] = (unsigned char)((size >> 0) & 255);
    data_start = 10;
  }

  int idx_response = 0;
  unsigned char* response = malloc(sizeof(unsigned char) * (data_start + size + 1));
  if (response == NULL) {
    return -1;
  }

  for (int i = 0; i < data_start; i++) {
    response[i] = frame[i];
    idx_response++;
  }

  for (int i = 0; i < size; i++) {
    response[idx_response] = msg[i];
    idx_response++;
  }

  response[idx_response] = '\0';

  ssize_t output = 0;
  sendn_client(client, response, idx_response, MSG_NOSIGNAL);

  free(response);
  return output;
}

static int send_close_frame(struct cws_frame* frame, int code)
{
  int cc;
  if (code != -1) {
    cc = code;
    goto custom_close;
  }

  if (frame->frame_size == 0 || frame->frame_size > 2) goto send;

  if (frame->frame_size == 1)
    cc = frame->msg_ctrl[0];
  else
    cc = ((int)frame->msg_ctrl[0]) << 8 | frame->msg_ctrl[1];

  if ((cc < 1000 || cc > 1003) && (cc < 1007 || cc > 1011) && (cc < 3000 || cc > 4999)) {
    cc = 1002;

  custom_close:
    frame->msg_ctrl[0] = (cc >> 8);
    frame->msg_ctrl[1] = (cc & 0xFF);

    if (cws_send(frame->client, (const char*)frame->msg_ctrl, sizeof(char) * 2, OP_CLOSE) < 0) {
      return -1;
    }

    return 0;
  }

send:
  if (cws_send(frame->client, (const char*)frame->msg_ctrl, frame->frame_size, OP_CLOSE) < 0) {
    return -1;
  }

  return 0;
}

static void close_client(cws_client_t* client)
{
  if (client == NULL || client->fd == -1) {
    return;
  }

  thread_mutex_lock(&g_clients_mutex);
  client->state = STATE_CLOSED;
  close_fd(client->fd);
  client->fd = -1;
  thread_mutex_term(&client->lock);
  thread_mutex_unlock(&g_clients_mutex);
}

static int recv_next_byte(struct cws_frame* frame)
{
  ssize_t n;

  if (frame->cur_pos == 0 || frame->cur_pos == frame->amt_read) {
    if ((n = recv(frame->client->fd, frame->buf, sizeof(frame->buf), 0)) <= 0) {
      frame->error = 1;
      return -1;
    }
    frame->amt_read = (size_t)n;
    frame->cur_pos = 0;
  }

  return (frame->buf[frame->cur_pos++]);
}

static int recv_frame(struct cws_frame* frame, struct cws_frame_state_data* fsd)
{
  uint64_t* frame_size;
  unsigned char* tmp;
  unsigned char* msg;
  uint64_t* msg_index;
  uint8_t* masks;
  int cur_byte;
  uint64_t i;

  if (fsd->opcode == OP_CLOSE || fsd->opcode == OP_PING || fsd->opcode == OP_PONG) {
    frame_size = &fsd->frame_size;
    msg_index = &fsd->msg_ctrl_index;
    masks = fsd->masks_ctrl;
    msg = fsd->msg_ctrl;
  } else {
    frame_size = &frame->frame_size;
    msg_index = &fsd->msg_data_index;
    masks = fsd->masks_data;
    msg = fsd->msg_data;
  }

  if (fsd->frame_length == 126) {
    fsd->frame_length = (((uint64_t)recv_next_byte(frame)) << 8) | recv_next_byte(frame);
  } else if (fsd->frame_length == 127) {
    fsd->frame_length = (((uint64_t)recv_next_byte(frame)) << 56) | (((uint64_t)recv_next_byte(frame)) << 48) |
                        (((uint64_t)recv_next_byte(frame)) << 40) | (((uint64_t)recv_next_byte(frame)) << 32) |
                        (((uint64_t)recv_next_byte(frame)) << 24) | (((uint64_t)recv_next_byte(frame)) << 16) |
                        (((uint64_t)recv_next_byte(frame)) << 8) | (((uint64_t)recv_next_byte(frame)));
  }

  *frame_size += fsd->frame_length;

  if (*frame_size > (16 * 1024 * 1024)) {
    frame->error = 1;
    return -1;
  }

  masks[0] = recv_next_byte(frame);
  masks[1] = recv_next_byte(frame);
  masks[2] = recv_next_byte(frame);
  masks[3] = recv_next_byte(frame);

  if (frame->error) {
    return -1;
  }

  if (fsd->frame_length > 0) {
    if (fsd->opcode != OP_CLOSE && fsd->opcode != OP_PING && fsd->opcode != OP_PONG) {
      tmp = realloc(msg, *msg_index + fsd->frame_length + fsd->is_fin);
      if (tmp == NULL) {
        frame->error = 1;
        return -1;
      }
      msg = tmp;
      fsd->msg_data = msg;
    }

    for (i = 0; i < fsd->frame_length; i++, (*msg_index)++) {
      cur_byte = recv_next_byte(frame);
      if (cur_byte == -1) {
        return -1;
      }

      msg[*msg_index] = cur_byte ^ masks[i % 4];
    }
  }

  if (fsd->is_fin && *frame_size > 0) {
    if (!fsd->frame_length && (fsd->opcode != OP_CLOSE && fsd->opcode != OP_PING && fsd->opcode != OP_PONG)) {
      tmp = realloc(msg, *msg_index + 1);
      if (tmp == NULL) {
        frame->error = 1;
        return -1;
      }
      msg = tmp;
      fsd->msg_data = msg;
    }
    msg[*msg_index] = '\0';
  }

  return 0;
}

int valid_utf8(uint8_t* s, size_t len, uint32_t state)
{
  // clang-format off
  static const uint8_t utf8d[] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 00..1f
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 20..3f
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 40..5f
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 60..7f
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9, // 80..9f
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7, // a0..bf
    8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, // c0..df
    0xa,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x4,0x3,0x3, // e0..ef
    0xb,0x6,0x6,0x6,0x5,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8, // f0..ff
    0x0,0x1,0x2,0x3,0x5,0x8,0x7,0x1,0x1,0x1,0x4,0x6,0x1,0x1,0x1,0x1, // s0..s0
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,0,1,0,1,1,1,1,1,1, // s1..s2
    1,2,1,1,1,1,1,2,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,1, // s3..s4
    1,2,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,3,1,3,1,1,1,1,1,1, // s5..s6
    1,3,1,1,1,1,1,3,1,3,1,1,1,1,1,1,1,3,1,1,1,1,1,1,1,1,1,1,1,1,1,1, // s7..s8
  };
  // clang-format on

  uint32_t codepoint = 0;

  for (ssize_t i = 0; i < len; i++) {
    uint32_t type = utf8d[s[i]];

    codepoint = (state != 0) ? (s[i] & 0x3fu) | (codepoint << 6) : (0xff >> type) & (s[i]);

    state = utf8d[256 + state * 16 + type];
  }

  return state;
}

static int recv_next_frame(struct cws_frame* frame)
{
  struct cws_frame_state_data fsd = {0};
  fsd.msg_data = NULL;
  fsd.msg_ctrl = frame->msg_ctrl;
  fsd.utf8_state = 0;

  frame->frame_size = 0;
  frame->op = -1;
  frame->msg = NULL;

  do {
    fsd.cur_byte = recv_next_byte(frame);
    if (fsd.cur_byte == -1) {
      return -1;
    }

    fsd.is_fin = (fsd.cur_byte & 0xFF) >> 7;
    fsd.opcode = (fsd.cur_byte & 0xF);

    if (fsd.cur_byte & 0x70) {
      frame->error = 1;
      break;
    }

    if ((frame->op == -1 && fsd.opcode == OP_CONTINUATION) ||
        (frame->op != -1 && (fsd.opcode != OP_CLOSE && fsd.opcode != OP_PING && fsd.opcode != OP_PONG) && fsd.opcode != OP_CONTINUATION)) {
      frame->error = 1;
      break;
    }

    // Fail if opcode isnt valid
    if (fsd.opcode != OP_CONTINUATION && fsd.opcode != OP_TEXT && fsd.opcode != OP_BINARY && fsd.opcode != OP_CLOSE && fsd.opcode != OP_PING &&
        fsd.opcode != OP_PONG) {
      frame->op = fsd.opcode;
      frame->error = 1;
    }
    thread_mutex_lock(&frame->client->lock);
    int state = frame->client->state;
    thread_mutex_unlock(&frame->client->lock);
    if (state == STATE_CLOSING && fsd.opcode != OP_CLOSE) {
      frame->error = 1;
      break;
    }

    if (fsd.opcode != OP_CONTINUATION && (fsd.opcode != OP_CLOSE && fsd.opcode != OP_PING && fsd.opcode != OP_PONG)) {
      frame->op = fsd.opcode;
    }

    fsd.mask = recv_next_byte(frame);
    fsd.frame_length = fsd.mask & 0x7F;
    fsd.frame_size = 0;
    fsd.msg_ctrl_index = 0;

    if ((fsd.opcode == OP_CLOSE || fsd.opcode == OP_PING || fsd.opcode == OP_PONG) && (!fsd.is_fin || fsd.frame_length > 125)) {
      frame->error = 1;
      break;
    }

    if (recv_frame(frame, &fsd) < 0) {
      break;
    }

    if (fsd.opcode == OP_CONTINUATION || fsd.opcode == OP_TEXT) {
      if (frame->op != OP_TEXT) {
        goto next_it;
      };

      if (fsd.is_fin) {
        if (valid_utf8(fsd.msg_data + (fsd.msg_data_index - fsd.frame_length), fsd.frame_length, 0) != 0) {
          frame->error = 1;
          send_close_frame(frame, 1007);
        }
        goto next_it;
      }

      fsd.utf8_state = valid_utf8(fsd.msg_data + (fsd.msg_data_index - fsd.frame_length), fsd.frame_length, fsd.utf8_state);
      if (fsd.utf8_state == 1) {
        frame->error = 1;
        send_close_frame(frame, 1007);
      }
    } else if (fsd.opcode == OP_CLOSE) {
      if (fsd.frame_size > 2 && valid_utf8(fsd.msg_ctrl + 2, fsd.frame_size - 2, 0) == 1) {
        frame->error = 1;
        break;
      }

      frame->op = OP_CLOSE;
      frame->frame_size = fsd.frame_size;
      free(fsd.msg_data);
      return 0;
    } else if (fsd.opcode == OP_PING) {
      if (cws_send(frame->client, (const char*)frame->msg_ctrl, fsd.frame_size, OP_PONG) < 0) {
        frame->error = 1;
        break;
      }
      fsd.is_fin = 0;
    } else if (fsd.opcode == OP_PONG) {
      fsd.is_fin = 0;
      if (fsd.frame_size != sizeof(frame->client->last_pong_id)) {
        goto next_it;
      }

      thread_mutex_lock(&frame->client->lock);
      fsd.pong_id = (fsd.msg_ctrl[3] << 0) | (fsd.msg_ctrl[2] << 8) | (fsd.msg_ctrl[1] << 16) | (fsd.msg_ctrl[0] << 24);
      if (fsd.pong_id < 0 || fsd.pong_id > frame->client->current_ping_id) {
        thread_mutex_unlock(&frame->client->lock);
        goto next_it;
      }
      frame->client->last_pong_id = fsd.pong_id;
      thread_mutex_unlock(&frame->client->lock);
    }

  next_it:;
  } while (!fsd.is_fin && !frame->error);

  if (frame->error) {
    free(fsd.msg_data);
    frame->msg = NULL;
    return -1;
  }

  frame->msg = fsd.msg_data;
  return 0;
}

static void sha1(const uint8_t* data, size_t size, uint8_t* output)
{
#define SHA1ROTATELEFT(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))
  uint32_t W[80];
  uint32_t H[] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  uint32_t a, b, c, d, e, f, k;
  uint32_t idx, lidx, widx, didx = 0;
  int32_t wcount;
  uint32_t temp;
  uint64_t sizebits = ((uint64_t)size) * 8;
  uint32_t loopcount = (size + 8) / 64 + 1;
  uint32_t tailbytes = 64 * loopcount - size;
  uint8_t datatail[128] = {0};

  datatail[0] = 0x80;
  datatail[tailbytes - 8] = (uint8_t)(sizebits >> 56 & 0xFF);
  datatail[tailbytes - 7] = (uint8_t)(sizebits >> 48 & 0xFF);
  datatail[tailbytes - 6] = (uint8_t)(sizebits >> 40 & 0xFF);
  datatail[tailbytes - 5] = (uint8_t)(sizebits >> 32 & 0xFF);
  datatail[tailbytes - 4] = (uint8_t)(sizebits >> 24 & 0xFF);
  datatail[tailbytes - 3] = (uint8_t)(sizebits >> 16 & 0xFF);
  datatail[tailbytes - 2] = (uint8_t)(sizebits >> 8 & 0xFF);
  datatail[tailbytes - 1] = (uint8_t)(sizebits >> 0 & 0xFF);

  for (lidx = 0; lidx < loopcount; lidx++) {
    memset(W, 0, 80 * sizeof(uint32_t));

    for (widx = 0; widx <= 15; widx++) {
      wcount = 24;

      while (didx < size && wcount >= 0) {
        W[widx] += (((uint32_t)data[didx]) << wcount);
        didx++;
        wcount -= 8;
      }
      while (wcount >= 0) {
        W[widx] += (((uint32_t)datatail[didx - size]) << wcount);
        didx++;
        wcount -= 8;
      }
    }

    for (widx = 16; widx <= 79; widx++) {
      W[widx] = SHA1ROTATELEFT((W[widx - 3] ^ W[widx - 8] ^ W[widx - 14] ^ W[widx - 16]), 1);
    }

    a = H[0];
    b = H[1];
    c = H[2];
    d = H[3];
    e = H[4];

    for (idx = 0; idx <= 79; idx++) {
      if (idx <= 19) {
        f = (b & c) | ((~b) & d);
        k = 0x5A827999;
      } else if (idx >= 20 && idx <= 39) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1;
      } else if (idx >= 40 && idx <= 59) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDC;
      } else if (idx >= 60 && idx <= 79) {
        f = b ^ c ^ d;
        k = 0xCA62C1D6;
      }
      temp = SHA1ROTATELEFT(a, 5) + f + e + k + W[idx];
      e = d;
      d = c;
      c = SHA1ROTATELEFT(b, 30);
      b = a;
      a = temp;
    }

    H[0] += a;
    H[1] += b;
    H[2] += c;
    H[3] += d;
    H[4] += e;
  }

  for (idx = 0; idx < 5; idx++) {
    output[idx * 4 + 0] = (uint8_t)(H[idx] >> 24);
    output[idx * 4 + 1] = (uint8_t)(H[idx] >> 16);
    output[idx * 4 + 2] = (uint8_t)(H[idx] >> 8);
    output[idx * 4 + 3] = (uint8_t)(H[idx]);
  }
}

static int handle_handshake(struct cws_frame* frame)
{
  ssize_t bytes_read;
  if ((bytes_read = recv(frame->client->fd, frame->buf, sizeof(frame->buf) - 1, 0)) < 0) {
    return -1;
  }

  char* key_start = strstr((const char*)frame->buf, "\r\n\r\n");
  if (key_start == NULL) {
    return -1;
  }

  frame->amt_read = bytes_read;
  frame->cur_pos = (size_t)((ptrdiff_t)(key_start - (char*)frame->buf)) + 4;

  char* save_ptr = NULL;
  for (key_start = strtok_r((char*)frame->buf, "\r\n", &save_ptr); key_start != NULL; key_start = strtok_r(NULL, "\r\n", &save_ptr)) {
    if (strstr(key_start, "Sec-WebSocket-Key") != NULL) {
      break;
    }
  }

  if (key_start == NULL) {
    return -1;
  }

  save_ptr = NULL;
  key_start = strtok_r(key_start, " ", &save_ptr);
  key_start = strtok_r(NULL, " ", &save_ptr);

  char* input = calloc(1, sizeof(char) * (61));
  if (input == NULL) {
    return -1;
  }

  strncpy(input, key_start, 24);
  strcat(input, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");

  unsigned char hash[20];
  sha1((const uint8_t*)input, 60, hash);
  unsigned char* base64_result = base64_encode(hash, 20);
  free(input);

  char* response = malloc(sizeof(char) * 130);
  if (response == NULL) {
    return -1;
  }

  strcpy(response, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ");
  strcat(response, (const char*)base64_result);
  strcat(response, "\r\n\r\n");
  free(base64_result);

  if (sendn_client(frame->client, response, strlen(response), MSG_NOSIGNAL) < 0) {
    free(response);
    return -1;
  }

  thread_mutex_lock(&frame->client->lock);
  frame->client->state = STATE_OPEN;
  thread_mutex_unlock(&frame->client->lock);

  frame->client->server->on_open(frame->client);
  free(response);
  return 0;
}

static int client_thread(void* data)
{
  cws_client_t* client = (cws_client_t*)data;
  struct cws_frame frame;

  memset(&frame, 0, sizeof(frame));
  frame.client = client;

  if (handle_handshake(&frame) < 0) {
    goto closed;
  }

  while (recv_next_frame(&frame) >= 0) {
    if ((frame.op == OP_TEXT || frame.op == OP_BINARY) && !frame.error) {
      client->server->on_message(client, frame.msg, frame.frame_size, frame.op);
    } else if (frame.op == OP_CLOSE && !frame.error) {
      thread_mutex_lock(&client->lock);
      int state = client->state;
      thread_mutex_unlock(&client->lock);
      if (state != STATE_CLOSING) {
        thread_mutex_lock(&client->lock);
        client->state = STATE_CLOSING;
        thread_mutex_unlock(&client->lock);
        send_close_frame(&frame, -1);
      }

      free(frame.msg);
      break;
    }

    free(frame.msg);
  }

  client->server->on_close(client);

closed:
  thread_mutex_lock(&client->lock);
  if (client->state != STATE_CLOSED) {
    close_client(client);
  }
  thread_mutex_unlock(&client->lock);

  return 0;
}

static int server_thread(void* data)
{
  struct cws_server_data* server_data = (struct cws_server_data*)data;

  struct sockaddr_storage address;
  socklen_t addrlen = sizeof(address);
  int client_fd;
  int i = 0;

  thread_mutex_init(&g_clients_mutex);

  while (thread_atomic_int_load(&g_exit_flag) == 0) {
    if ((client_fd = accept(server_data->fd, (struct sockaddr*)&address, &addrlen)) < 0) {
      continue;
    }

    thread_mutex_lock(&g_clients_mutex);
    for (i = 0; i < MAX_CLIENTS; i++) {
      if (g_clients[i].fd == -1) {
        g_clients[i].fd = client_fd;
        g_clients[i].state = STATE_CONNECTING;
        g_clients[i].server = &server_data->server;
        thread_mutex_init(&g_clients[i].lock);

        break;
      }
    }
    thread_mutex_unlock(&g_clients_mutex);

    if (i != MAX_CLIENTS) {
      thread_ptr_t thread = thread_create(client_thread, &g_clients[i], THREAD_STACK_SIZE_DEFAULT);
      thread_detach(thread);
    } else {
      close_fd(client_fd);
    }
  }

  return 0;
}

int cws_start(cws_server_t server)
{
  memset(&g_server_data, -1, sizeof(g_server_data));
  memcpy(&g_server_data.server, &server, sizeof(cws_server_t));
  memset(g_clients, -1, sizeof(g_clients));

#ifdef _WIN32
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    return 1;
  }
  setvbuf(stdout, NULL, _IONBF, 0);
#endif

  if ((g_server_data.fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    return 2;
  }

  int reuse = 1;
  if (setsockopt(g_server_data.fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0) {
    close_fd(g_server_data.fd);
    return 3;
  }

  struct sockaddr_in address;
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(g_server_data.server.port);

  if (bind(g_server_data.fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
    close_fd(g_server_data.fd);
    return 4;
  }

  if (listen(g_server_data.fd, MAX_CLIENTS) < 0) {
    close_fd(g_server_data.fd);
    return 5;
  }

  thread_atomic_int_store(&g_exit_flag, 0);
  thread_ptr_t thread = thread_create(server_thread, &g_server_data, THREAD_STACK_SIZE_DEFAULT);
  thread_detach(thread);

  return 0;
}

int cws_stop()
{
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (g_clients[i].fd != -1) {
      close_client(&g_clients[i]);
    }
  }

  thread_atomic_int_store(&g_exit_flag, 1);
  thread_mutex_term(&g_clients_mutex);

  close_fd(g_server_data.fd);

#ifdef _WIN32
  WSACleanup();
#endif

  return 0;
}