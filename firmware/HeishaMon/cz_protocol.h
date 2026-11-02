#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

// Framing and proxy decisions are independent of Arduino and tested at compile time.
namespace cz {
constexpr size_t MaxFrame = 255;
constexpr uint32_t FrameTimeoutMs = 2000;
constexpr uint32_t CacheLifetimeMs = 30000;
constexpr bool freshCache(bool valid, uint32_t now, uint32_t storedAt) {
  return valid && uint32_t(now-storedAt) <= CacheLifetimeMs;
}
enum class Feed { Waiting, Complete, BadHeader, BadLength, BadChecksum };
enum class Action { Drop, ReplyCache, Forward, ForwardAndReplyCache };

constexpr bool validFrame(const uint8_t *bytes, size_t size) {
  if (size < 4 || size > MaxFrame || size != size_t(bytes[1]) + 3 || bytes[2] != 1) return false;
  if (bytes[0] != 0x71 && bytes[0] != 0x31 && bytes[0] != 0xf1) return false;
  uint8_t sum = 0;
  for (size_t i = 0; i < size; ++i) sum += bytes[i];
  return sum == 0;
}

struct Framer {
  std::array<uint8_t, MaxFrame> bytes{};
  size_t used = 0;
  size_t expected = 0;
  uint32_t lastByte = 0;
  bool ready = false;
  bool allowWrite = true;

  constexpr explicit Framer(bool writes = true) : allowWrite(writes) {}
  constexpr void reset() { used = expected = 0; ready = false; }
  constexpr Feed push(uint8_t value, uint32_t now) {
    if (ready || (used && uint32_t(now - lastByte) > FrameTimeoutMs)) reset();
    lastByte = now;
    if (!used && value != 0x71 && value != 0x31 && !(allowWrite && value == 0xf1)) return Feed::BadHeader;
    bytes[used++] = value;
    if (used == 2) {
      expected = size_t(bytes[1]) + 3;
      if (expected < 4 || expected > MaxFrame) { reset(); return Feed::BadLength; }
    }
    if (used == 3 && bytes[2] != 1) { reset(); return Feed::BadHeader; }
    if (expected && used == expected) {
      if (!validFrame(bytes.data(), used)) { reset(); return Feed::BadChecksum; }
      ready = true;
      return Feed::Complete;
    }
    return Feed::Waiting;
  }
};

constexpr bool basicData(const uint8_t *bytes, size_t size) {
  return size == 203 && bytes[0] == 0x71 && bytes[1] == 0xc8 && bytes[2] == 1 && bytes[3] == 0x10 && validFrame(bytes,size);
}

constexpr Action requestAction(const uint8_t *bytes, size_t size, bool enabled, bool listenOnly, bool freshCache) {
  if (!enabled || listenOnly || !validFrame(bytes,size)) return Action::Drop;
  const bool basicQuery = size == 111 && (bytes[0] == 0x71 || bytes[0] == 0xf1) && bytes[3] == 0x10;
  if (basicQuery && freshCache) return bytes[0] == 0xf1 ? Action::ForwardAndReplyCache : Action::ReplyCache;
  return Action::Forward;
}
}
