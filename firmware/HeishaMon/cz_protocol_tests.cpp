#include "cz_protocol.h"
namespace {
constexpr auto query(uint8_t header = 0x71, uint8_t type = 0x10) {
  std::array<uint8_t,111> frame{};
  frame[0] = header; frame[1] = 0x6c; frame[2] = 1; frame[3] = type;
  frame[110] = uint8_t(0 - header - 0x6c - 1 - type);
  return frame;
}
constexpr auto read = query();
constexpr auto write = query(0xf1);
constexpr auto extra = query(0x71,0x21);
constexpr std::array<uint8_t,4> startup{0x31,0x01,0x01,0xcd};
constexpr auto basicResponse() {
  std::array<uint8_t,203> response{};
  response[0]=0x71; response[1]=0xc8; response[2]=1; response[3]=0x10; response[202]=0xb6;
  return response;
}
constexpr auto response = basicResponse();
static_assert(cz::basicData(response.data(),response.size()));
static_assert(cz::freshCache(true,30000,0));
static_assert(!cz::freshCache(true,30001,0));
static_assert(!cz::freshCache(false,0,0));
static_assert(cz::freshCache(true,0x10,0xfffffff0));
static_assert(read[110] == 0x12); // Published v2.0 Panasonic query checksum.
static_assert(cz::validFrame(read.data(),read.size()));
static_assert(cz::validFrame(startup.data(),startup.size()));
static_assert(cz::requestAction(read.data(),111,true,false,true) == cz::Action::ReplyCache);
static_assert(cz::requestAction(write.data(),111,true,false,true) == cz::Action::ForwardAndReplyCache);
static_assert(cz::requestAction(read.data(),111,true,false,false) == cz::Action::Forward);
static_assert(cz::requestAction(extra.data(),111,true,false,true) == cz::Action::Forward);
static_assert(cz::requestAction(startup.data(),4,true,false,true) == cz::Action::Forward);
static_assert(cz::requestAction(write.data(),111,true,true,true) == cz::Action::Drop);
static_assert(cz::requestAction(read.data(),111,false,false,true) == cz::Action::Drop);

constexpr bool fragmentedAndConsecutive() {
  cz::Framer frame;
  for (size_t i=0; i<read.size(); ++i) {
    const auto result = frame.push(read[i],uint32_t(i));
    if (result != (i == read.size()-1 ? cz::Feed::Complete : cz::Feed::Waiting)) return false;
  }
  for (size_t i=0; i<startup.size(); ++i) {
    const auto result = frame.push(startup[i],uint32_t(200+i));
    if (result != (i == startup.size()-1 ? cz::Feed::Complete : cz::Feed::Waiting)) return false;
  }
  return frame.used == 4 && frame.ready;
}
constexpr bool corruptedAndOversized() {
  cz::Framer frame;
  for (size_t i=0; i<read.size()-1; ++i) frame.push(read[i],0);
  if (frame.push(0,0) != cz::Feed::BadChecksum || frame.used) return false;
  frame.push(0x71,1);
  if (frame.push(0xff,1) != cz::Feed::BadLength || frame.used) return false;
  frame.push(0x71,2);
  return frame.push(0,2) == cz::Feed::BadLength && !frame.used;
}
constexpr bool timeoutAndClockRollover() {
  cz::Framer frame;
  frame.push(0x71,0);
  if (frame.push(0x31,3000) != cz::Feed::Waiting || frame.used != 1) return false;
  frame.reset();
  frame.push(0x31,0xfffffff0);
  frame.push(1,0x10);
  frame.push(1,0x11);
  return frame.push(0xcd,0x12) == cz::Feed::Complete;
}
constexpr bool rejectEchoedWritesOnPdc() {
  cz::Framer frame(false);
  return frame.push(0xf1,0) == cz::Feed::BadHeader && frame.used == 0;
}
constexpr bool maximumLengthAndResync() {
  cz::Framer frame;
  for (size_t i=0; i<255; ++i) {
    const uint8_t value = i == 0 ? 0x71 : i == 1 ? 0xfc : i == 2 ? 1 : i == 254 ? 0x92 : 0;
    const auto result = frame.push(value,uint32_t(i));
    if (result != (i == 254 ? cz::Feed::Complete : cz::Feed::Waiting)) return false;
  }
  return frame.used == 255 && frame.push(0x31,300) == cz::Feed::Waiting && frame.used == 1;
}
static_assert(fragmentedAndConsecutive());
static_assert(corruptedAndOversized());
static_assert(timeoutAndClockRollover());
static_assert(rejectEchoedWritesOnPdc());
static_assert(maximumLengthAndResync());
}
