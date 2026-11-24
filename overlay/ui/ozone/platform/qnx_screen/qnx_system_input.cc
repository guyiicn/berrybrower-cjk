// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license.

#include "ui/ozone/platform/qnx_screen/qnx_system_input.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <stdlib.h>
#include <sys/socket.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <utility>

#include "base/base_paths.h"
#include "base/files/scoped_file.h"
#include "base/memory/weak_ptr.h"
#include "base/path_service.h"
#include "base/posix/eintr_wrapper.h"
#include "base/process/kill.h"
#include "base/strings/utf_string_conversions.h"
#include "base/timer/timer.h"
#include "deploy/berry-shell-bar/berry_ime_protocol.h"

namespace ui {

void TraceQnxIme(const char* event, int first, int second) {
  static const bool enabled = getenv("BERRY_IME_DIAGNOSTICS") != nullptr;
  if (!enabled) {
    return;
  }
  static std::atomic<unsigned> count{0};
  if (count.fetch_add(1, std::memory_order_relaxed) >= 256) {
    return;
  }
  fprintf(stderr, "BerryIME: %s %d %d\n", event, first, second);
}

class QnxSystemInput::Session {
 public:
  Session(EditCallback edit, KeyCallback forward_key)
      : edit_(std::move(edit)), forward_key_(std::move(forward_key)) {}

  ~Session() {
    socket_.reset();
    if (pid_ > 0) {
      base::EnsureProcessTerminated(base::Process(pid_));
    }
  }

  bool Open() {
    base::FilePath directory;
    if (!base::PathService::Get(base::DIR_EXE, &directory)) {
      return false;
    }
    auto helper = directory.AppendASCII("berry_ime_helper");
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) {
      return false;
    }
    base::ScopedFD parent(pair[0]);
    base::ScopedFD child(pair[1]);
    if (fcntl(parent.get(), F_SETFD, FD_CLOEXEC) < 0 ||
        fcntl(child.get(), F_SETFD, FD_CLOEXEC) < 0) {
      return false;
    }

    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
      return false;
    }
    int status =
        posix_spawn_file_actions_adddup2(&actions, child.get(), BERRY_IME_FD);
    if (status == 0) {
      char* argv[] = {const_cast<char*>(helper.value().c_str()), nullptr};
      // QNX's system input client must use the system libraries. The browser
      // normally publishes QNX 8 library overrides in its environment.
      char* env[] = {nullptr};
      status = posix_spawn(&pid_, argv[0], &actions, nullptr, argv, env);
    }
    posix_spawn_file_actions_destroy(&actions);
    if (status != 0) {
      TraceQnxIme("helper-spawn-error", status);
      pid_ = -1;
      return false;
    }

    child.reset();
    socket_ = std::move(parent);
    const auto deadline = base::TimeTicks::Now() + base::Milliseconds(250);
    while (base::TimeTicks::Now() < deadline) {
      const auto remaining = deadline - base::TimeTicks::Now();
      pollfd descriptor{socket_.get(), POLLIN, 0};
      if (HANDLE_EINTR(
              poll(&descriptor, 1,
                   std::max<int64_t>(1, remaining.InMilliseconds()))) <= 0) {
        break;
      }
      if (Receive()) {
        const bool ready =
            incoming_.type == BERRY_IME_READY && incoming_.a == 0;
        TraceQnxIme("helper-focus", incoming_.a, ready);
        if (!ready) {
          return false;
        }
        timer_.Start(FROM_HERE, base::Milliseconds(8), this, &Session::Drain);
        return true;
      }
      if (!socket_.is_valid()) {
        break;
      }
    }
    TraceQnxIme("helper-no-ready");
    return false;
  }

  bool IsOpen() const { return socket_.is_valid(); }

  void SetText(std::u16string text, size_t anchor, size_t cursor) {
    if (has_context_ && text == text_ && anchor == anchor_ &&
        cursor == cursor_) {
      return;
    }
    has_context_ = true;
    text_ = std::move(text);
    anchor_ = anchor;
    cursor_ = cursor;
    BerryImeMessage message{};
    message.type = BERRY_IME_CONTEXT;
    const std::wstring wide = base::UTF16ToWide(text_);
    if (wide.size() <= BERRY_IME_TEXT_LIMIT) {
      message.length = wide.size();
      const auto index = [&](size_t offset) {
        return base::UTF16ToWide(std::u16string_view(text_).substr(
                                     0, std::min(text_.size(), offset)))
            .size();
      };
      message.a = index(anchor);
      message.b = index(cursor);
      std::copy(wide.begin(), wide.end(), message.text);
    }
    Send(message);
  }

  bool DispatchKey(QnxRawKey raw, bool pressed) {
    BerryImeMessage message{};
    message.type = BERRY_IME_KEY;
    message.a = raw.code;
    message.b = raw.character;
    message.c = raw.modifiers;
    message.d = pressed;
    message.timestamp = raw.timestamp_ns;
    const bool sent = Send(message);
    TraceQnxIme("helper-key", pressed, sent);
    return sent;
  }

  void CancelComposition() {
    BerryImeMessage message{};
    message.type = BERRY_IME_CANCEL;
    Send(message);
  }

 private:
  bool Send(const BerryImeMessage& message) {
    if (!socket_.is_valid()) {
      return false;
    }
    const char* data = reinterpret_cast<const char*>(&message);
    size_t sent = 0;
    while (sent < sizeof(message)) {
      const ssize_t n =
          HANDLE_EINTR(send(socket_.get(), data + sent, sizeof(message) - sent,
                            MSG_DONTWAIT | MSG_NOSIGNAL));
      if (n <= 0) {
        if (sent != 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
          socket_.reset();
        }
        return false;
      }
      sent += n;
    }
    return true;
  }

  bool Receive() {
    if (!socket_.is_valid()) {
      return false;
    }
    char* data = reinterpret_cast<char*>(&incoming_);
    while (received_ < sizeof(incoming_)) {
      const ssize_t n =
          HANDLE_EINTR(recv(socket_.get(), data + received_,
                            sizeof(incoming_) - received_, MSG_DONTWAIT));
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return false;
      }
      if (n <= 0) {
        socket_.reset();
        return false;
      }
      received_ += n;
    }
    received_ = 0;
    return true;
  }

  void Drain() {
    const auto weak = weak_factory_.GetWeakPtr();
    for (int i = 0; i < 32 && Receive(); ++i) {
      const auto& message = incoming_;
      if (message.type == BERRY_IME_FORWARD) {
        TraceQnxIme("forwarded-key", message.d);
        forward_key_.Run(message.a, message.b, message.c, message.d != 0);
      } else if (message.type >= BERRY_IME_COMMIT &&
                 message.type <= BERRY_IME_COMPOSE_REGION &&
                 message.length <= BERRY_IME_TEXT_LIMIT) {
        const Edit edit = static_cast<Edit>(message.type - BERRY_IME_COMMIT);
        std::wstring text(message.text, message.text + message.length);
        TraceQnxIme("edit", static_cast<int>(edit), message.length);
        edit_.Run(edit, base::WideToUTF16(text), message.a, message.b);
      }
      if (!weak) {
        return;
      }
    }
    if (!socket_.is_valid()) {
      timer_.Stop();
    }
  }

  EditCallback edit_;
  KeyCallback forward_key_;
  base::ScopedFD socket_;
  pid_t pid_ = -1;
  BerryImeMessage incoming_{};
  size_t received_ = 0;
  bool has_context_ = false;
  std::u16string text_;
  size_t anchor_ = 0;
  size_t cursor_ = 0;
  base::RepeatingTimer timer_;
  base::WeakPtrFactory<Session> weak_factory_{this};
};

QnxSystemInput::QnxSystemInput(EditCallback edit, KeyCallback forward_key)
    : edit_(std::move(edit)), forward_key_(std::move(forward_key)) {}

namespace {
QnxSystemInput* active_input = nullptr;
}

QnxSystemInput::~QnxSystemInput() {
  Blur();
}

bool QnxSystemInput::Focus(bool priority) {
  if (session_ && session_->IsOpen()) {
    return true;
  }
  if (active_input && active_input->priority_ && !priority) {
    return false;
  }
  if (active_input) {
    active_input->Blur();
  }
  auto session = std::make_unique<Session>(edit_, forward_key_);
  if (!session->Open()) {
    return false;
  }
  session_ = std::move(session);
  active_input = this;
  priority_ = priority;
  return true;
}

void QnxSystemInput::Blur() {
  if (active_input == this) {
    active_input = nullptr;
  }
  session_.reset();
}

void QnxSystemInput::SetSurroundingText(std::u16string text,
                                        size_t anchor,
                                        size_t cursor) {
  if (session_) {
    session_->SetText(std::move(text), anchor, cursor);
  }
}

bool QnxSystemInput::DispatchKey(QnxRawKey raw, bool pressed) {
  return session_ && session_->DispatchKey(raw, pressed);
}

void QnxSystemInput::CancelComposition() {
  if (session_) {
    session_->CancelComposition();
  }
}

}  // namespace ui
