// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license.

#ifndef UI_OZONE_PLATFORM_QNX_SCREEN_QNX_SYSTEM_INPUT_H_
#define UI_OZONE_PLATFORM_QNX_SCREEN_QNX_SYSTEM_INPUT_H_

#include <stdint.h>

#include <memory>
#include <string>

#include "base/functional/callback.h"

namespace ui {

// Connects an editable field to the BlackBerry 10 system input method.
inline constexpr char kQnxRawKeyProperty[] = "qnx.raw-key";

struct QnxRawKey {
  int code;
  int character;
  int modifiers;
  uint64_t timestamp_ns = 0;
};

void TraceQnxIme(const char* event, int first = 0, int second = 0);

class KeyEvent;
std::unique_ptr<KeyEvent> MakeQnxForwardedKey(QnxRawKey raw, bool pressed);

class QnxSystemInput {
 public:
  enum class Edit {
    kCommit,
    kCompose,
    kFinish,
    kDelete,
    kSelect,
    kComposeRegion
  };
  using EditCallback =
      base::RepeatingCallback<void(Edit, std::u16string, int, int)>;
  using KeyCallback = base::RepeatingCallback<void(int, int, int, bool)>;

  QnxSystemInput(EditCallback edit, KeyCallback forward_key);
  ~QnxSystemInput();
  QnxSystemInput(const QnxSystemInput&) = delete;
  QnxSystemInput& operator=(const QnxSystemInput&) = delete;

  bool Focus(bool priority = false);
  void Blur();
  void SetSurroundingText(std::u16string text, size_t anchor, size_t cursor);
  bool DispatchKey(QnxRawKey raw, bool pressed);
  void CancelComposition();

 private:
  class Session;
  EditCallback edit_;
  KeyCallback forward_key_;
  std::unique_ptr<Session> session_;
  bool priority_ = false;
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_QNX_SCREEN_QNX_SYSTEM_INPUT_H_
