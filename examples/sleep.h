#ifndef SLEEP_H
#define SLEEP_H

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

static inline void sleep_ms(int ms)
{
#ifdef _WIN32
  Sleep(ms);
#else
  usleep(ms * 1000);
#endif
}

#endif // SLEEP_H
