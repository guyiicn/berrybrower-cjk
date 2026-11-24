/* Process-local protocol between BerryBrowser and the BB10 IMF helper. */
#ifndef BERRY_IME_PROTOCOL_H_
#define BERRY_IME_PROTOCOL_H_
#include <stdint.h>
#define BERRY_IME_FD 0
#define BERRY_IME_TEXT_LIMIT 1024

enum {
  BERRY_IME_CONTEXT = 1,
  BERRY_IME_KEY,
  BERRY_IME_CANCEL,
  BERRY_IME_READY,
  BERRY_IME_FORWARD,
  BERRY_IME_COMMIT,
  BERRY_IME_COMPOSE,
  BERRY_IME_FINISH,
  BERRY_IME_DELETE,
  BERRY_IME_SELECT,
  BERRY_IME_COMPOSE_REGION
};

typedef struct {
  uint64_t timestamp;
  uint32_t type;
  uint32_t length;
  int32_t a, b, c, d;
  uint32_t text[BERRY_IME_TEXT_LIMIT];
} BerryImeMessage;
#endif
