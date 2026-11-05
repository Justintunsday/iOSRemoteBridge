# Literal Unicode input

`insert_text` bypasses physical-key translation and IME candidate selection.
The existing `type_text` operation keeps its ASCII scancode behavior. Both
remain guarded by the paired control session and the existing lock check.

The new operation uses one UTF-16LE `IOHIDEventCreateUnicodeEvent` dispatched
through the existing `IOHIDEventSystemClient`. It does not require keyboard
mode, use a clipboard, reuse the touchscreen sender, or fall back after a
dispatch. Whole-string validation rejects malformed surrogate pairs and text
longer than 1024 UTF-16 code units before input is submitted.

## Device observation, 2026-11-05

An isolated UIKit fixture on iPhone SE 2, iOS 16.1.1 / 20B101 compiled and
called the actual modified `IRBInput.m`. No VNC connection was opened. The
fixture was foreground and its own UITextView was first responder. With the
Chinese `zh-Hans` input mode active, readback exactly matched:

- Mixed Chinese, Latin, emoji, a ZWJ emoji sequence, an accented letter and newline.
- 512 supplementary-plane emoji (1024 UTF-16 units).
- 1024 ASCII letters.
- Combining accent, consecutive newlines, Chinese lines and tab.
- Empty text (no input event).

All accepted nonempty cases had no marked-text composition after insertion.
An over-limit string was rejected and the field stayed empty. The production
method sent no field-4 override or sender ID. A separate targeted
`BKSHIDEventSendToProcess` probe produced no text and is not used.

This proves the HID Unicode method in the owned fixture. It does not by
itself prove a deployed SpringBoard/MCP round trip, secure fields, arbitrary
custom editors or other iOS builds. A result with `delivery_verified:false`
must still be followed by observation of the intended field.
