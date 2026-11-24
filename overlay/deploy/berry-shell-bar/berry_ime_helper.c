/* BB10 input-service adapter. Linked against the system C runtime only. */
#include "berry_ime_protocol.h"

#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/keycodes.h>
#include <sys/socket.h>
#include <unistd.h>
#include <wchar.h>

typedef struct {
  uint32_t pid, component;
} Session;
typedef struct {
  uint32_t id, type, pid, component, serial, flags, reserved;
} Event;
typedef struct {
  Event event;
  uint32_t pad;
  uint64_t style;
  uint32_t reserved[4];
} Focus;
typedef struct {
  Event event;
  int32_t code, character, modifiers;
  uint32_t sequence, pad;
  uint64_t timestamp;
  uint32_t reserved[4];
} Key;
typedef struct {
  Event event;
  uint32_t previous, current, reserved;
} Caret;
typedef struct {
  wchar_t* text;
  uint32_t count;
  void* spans;
  uint32_t span_count;
} Text;
typedef void (*Callback)(void);

_Static_assert(sizeof(Focus) == 56 && sizeof(Key) == 72 &&
                   sizeof(Caret) == 40 && sizeof(wchar_t) == 4,
               "BB10 IMF ABI");

static Session* session;
static int (*dispatch)(Event*);
static uint32_t serial, sequence;
static int has_caret;
static pthread_mutex_t output_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;
static wchar_t surrounding[BERRY_IME_TEXT_LIMIT];
static size_t length, anchor, cursor;
static int composing;
static int forwarded_backspace;

static int send_message(const BerryImeMessage* message) {
  size_t sent = 0;
  pthread_mutex_lock(&output_lock);
  while (sent < sizeof(*message)) {
    ssize_t n = send(BERRY_IME_FD, (const char*)message + sent,
                     sizeof(*message) - sent, 0);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n <= 0) {
      break;
    }
    sent += n;
  }
  pthread_mutex_unlock(&output_lock);
  return sent == sizeof(*message) ? 0 : -1;
}

static int read_message(BerryImeMessage* message) {
  size_t received = 0;
  while (received < sizeof(*message)) {
    ssize_t n = read(BERRY_IME_FD, (char*)message + received,
                     sizeof(*message) - received);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n <= 0) {
      return -1;
    }
    received += n;
  }
  return 0;
}

static Event header(unsigned id, unsigned type) {
  Event result = {id, type, session->pid, session->component, ++serial, 0, 0};
  return result;
}

static int edit(unsigned type, const Text* text, int a, int b) {
  BerryImeMessage message = {.type = type, .a = a, .b = b};
  if (text) {
    if (text->count > BERRY_IME_TEXT_LIMIT || (text->count && !text->text)) {
      return -1;
    }
    message.length = text->count;
    for (size_t i = 0; i < message.length; ++i) {
      message.text[i] = text->text[i];
    }
  }
  if (type == BERRY_IME_COMMIT || type == BERRY_IME_COMPOSE ||
      type == BERRY_IME_FINISH || type == BERRY_IME_COMPOSE_REGION) {
    pthread_mutex_lock(&state_lock);
    composing = (type == BERRY_IME_COMPOSE && message.length != 0) ||
                (type == BERRY_IME_COMPOSE_REGION && b > a);
    pthread_mutex_unlock(&state_lock);
  }
  return send_message(&message);
}

static int batch(Session* s) {
  (void)s;
  return 0;
}
static int commit(Session* s, Text* t, int offset) {
  (void)s;
  return edit(BERRY_IME_COMMIT, t, offset, 0);
}
static int compose(Session* s, Text* t, int offset) {
  (void)s;
  return edit(BERRY_IME_COMPOSE, t, offset, 0);
}
static int finish(Session* s) {
  (void)s;
  return edit(BERRY_IME_FINISH, NULL, 0, 0);
}
static int remove_text(Session* s, int a, int b) {
  (void)s;
  return edit(BERRY_IME_DELETE, NULL, a, b);
}
static int select_text(Session* s, int a, int b) {
  (void)s;
  return edit(BERRY_IME_SELECT, NULL, a, b);
}
static int compose_region(Session* s, int a, int b) {
  (void)s;
  return edit(BERRY_IME_COMPOSE_REGION, NULL, a, b);
}

static int get_cursor(Session* s) {
  (void)s;
  pthread_mutex_lock(&state_lock);
  int position = cursor;
  pthread_mutex_unlock(&state_lock);
  return position;
}

static Text* copy_text(size_t first, size_t count) {
  Text* result = calloc(1, sizeof(*result));
  if (!result) {
    return NULL;
  }
  result->text = calloc(count + 1, sizeof(wchar_t));
  if (!result->text) {
    free(result);
    return NULL;
  }
  memcpy(result->text, surrounding + first, count * sizeof(wchar_t));
  result->count = count;
  return result;
}

static Text* before(Session* s, int n, int flags) {
  (void)s;
  (void)flags;
  if (n < 0) {
    return NULL;
  }
  pthread_mutex_lock(&state_lock);
  size_t count = (size_t)n < cursor ? (size_t)n : cursor;
  Text* text = copy_text(cursor - count, count);
  pthread_mutex_unlock(&state_lock);
  return text;
}

static Text* after(Session* s, int n, int flags) {
  (void)s;
  (void)flags;
  if (n < 0) {
    return NULL;
  }
  pthread_mutex_lock(&state_lock);
  size_t available = length - cursor;
  Text* text = copy_text(cursor, (size_t)n < available ? (size_t)n : available);
  pthread_mutex_unlock(&state_lock);
  return text;
}

static Text* selected(Session* s, int flags) {
  (void)s;
  (void)flags;
  pthread_mutex_lock(&state_lock);
  size_t min = cursor < anchor ? cursor : anchor;
  size_t max = cursor > anchor ? cursor : anchor;
  Text* text = copy_text(min, max - min);
  pthread_mutex_unlock(&state_lock);
  return text;
}

static int is_selected(Session* s, int* result) {
  (void)s;
  if (!result) {
    return -1;
  }
  pthread_mutex_lock(&state_lock);
  *result = cursor != anchor;
  pthread_mutex_unlock(&state_lock);
  return 0;
}

static int is_all_selected(Session* s, int* result) {
  (void)s;
  if (!result) {
    return -1;
  }
  pthread_mutex_lock(&state_lock);
  *result = length && ((cursor == 0 && anchor == length) ||
                       (anchor == 0 && cursor == length));
  pthread_mutex_unlock(&state_lock);
  return 0;
}

static int receive_event(Session* s, Event* event) {
  (void)s;
  if (!event) {
    return -1;
  }
  if (event->type == 2 && event->id == 1) {
    return 0;
  }
  if (event->type != 1 || event->id < 1 || event->id > 3) {
    return -1;
  }
  Key* key = (Key*)event;
  BerryImeMessage message = {.type = BERRY_IME_FORWARD,
                             .a = key->code,
                             .b = key->character,
                             .c = key->modifiers,
                             .d = event->id != 2};
  return send_message(&message);
}

static Callback callbacks[26] = {
    [0] = (Callback)batch,          [2] = (Callback)commit,
    [3] = (Callback)remove_text,    [4] = (Callback)batch,
    [5] = (Callback)finish,         [7] = (Callback)get_cursor,
    [9] = (Callback)selected,       [10] = (Callback)after,
    [11] = (Callback)before,        [15] = (Callback)receive_event,
    [16] = (Callback)receive_event, [17] = (Callback)compose_region,
    [18] = (Callback)compose,       [19] = (Callback)select_text,
    [23] = (Callback)is_selected,   [24] = (Callback)is_all_selected,
};

static void initialize_caret(unsigned position) {
  if (has_caret) {
    return;
  }
  Caret caret = {header(1, 2), position, position, 0};
  caret.event.flags = 2;
  has_caret = dispatch(&caret.event) == 0;
}

static int forward_backspace(const BerryImeMessage* message) {
  const int key = message->b ? message->b : message->a;
  if (key != KEYCODE_BACKSPACE && key != '\b') {
    return 0;
  }
  if (message->d) {
    pthread_mutex_lock(&state_lock);
    forwarded_backspace = !composing;
    pthread_mutex_unlock(&state_lock);
    return forwarded_backspace;
  }
  const int forward = forwarded_backspace;
  forwarded_backspace = 0;
  return forward;
}

static void handle_command(BerryImeMessage* message) {
  switch (message->type) {
    case BERRY_IME_CONTEXT:
      if (message->length > BERRY_IME_TEXT_LIMIT) {
        return;
      }
      pthread_mutex_lock(&state_lock);
      length = message->length;
      cursor = message->b >= 0 && (size_t)message->b <= length
                   ? (size_t)message->b
                   : length;
      anchor = message->a >= 0 && (size_t)message->a <= length
                   ? (size_t)message->a
                   : cursor;
      for (size_t i = 0; i < length; ++i) {
        surrounding[i] = message->text[i];
      }
      pthread_mutex_unlock(&state_lock);
      initialize_caret(cursor);
      break;
    case BERRY_IME_KEY: {
      // BB10 may use Backspace to reopen the previous committed phrase.
      // Outside active composition, the browser owns normal deletion.
      if (forward_backspace(message)) {
        message->type = BERRY_IME_FORWARD;
        send_message(message);
        break;
      }
      initialize_caret(0);
      Key key = {.event = header(message->d ? 1 : 2, 1),
                 .code = message->a,
                 .character = message->b,
                 .modifiers = message->c,
                 .timestamp = message->timestamp};
      if (message->d) {
        ++sequence;
      }
      key.sequence = sequence;
      if (dispatch(&key.event) != 0) {
        message->type = BERRY_IME_FORWARD;
        send_message(message);
      }
      break;
    }
    case BERRY_IME_CANCEL: {
      struct {
        Event event;
        uint32_t arguments[2];
      } action = {header(1, 5), {0, 0}};
      dispatch(&action.event);
      break;
    }
    default:
      break;
  }
}

int main(void) {
  signal(SIGPIPE, SIG_IGN);
  void* lib = dlopen("libinput_client.so.1", RTLD_NOW | RTLD_LOCAL);
  if (!lib) {
    return 1;
  }
  int (*init)(void) = dlsym(lib, "imf_client_init");
  Session* (*open_session)(Callback*) = dlsym(lib, "ictrl_open_session");
  void (*close_session)(Session*) = dlsym(lib, "ictrl_close_session");
  void (*disconnect)(void) = dlsym(lib, "imf_client_disconnect");
  dispatch = dlsym(lib, "ictrl_dispatch_event");
  if (!init || !open_session || !close_session || !dispatch || init() != 0) {
    return 2;
  }
  session = open_session(callbacks);
  if (!session) {
    return 3;
  }

  Focus focus = {.event = header(1, 4), .style = 0x7c};
  BerryImeMessage ready = {.type = BERRY_IME_READY,
                           .a = dispatch(&focus.event)};
  if (send_message(&ready) == 0 && ready.a == 0) {
    BerryImeMessage message;
    while (read_message(&message) == 0) {
      handle_command(&message);
    }
  }
  focus.event = header(2, 4);
  dispatch(&focus.event);
  close_session(session);
  if (disconnect) {
    disconnect();
  }
  return 0;
}
