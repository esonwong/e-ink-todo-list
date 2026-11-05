#pragma once
// Streaming decoder for the `rle1` bitmap encoding.
//
// The server (einktodo/utils/rle-bitmap.ts) is the format's reference; keep
// the two in sync. Stream = blocks, each starting with a token byte T:
//   T < 0x80   literal: the next T+1 bytes are copied verbatim (1..128)
//   T >= 0x80  run: k = T & 0x7F.  k < 127 → L = k + 3;  k == 127 → L = 130 +
//              unsigned LEB128 varint that follows.  Then one value byte,
//              repeated L times.
//
// Designed for the ESP8266 read loop: feed() takes bytes as they arrive from
// the socket and hands decoded data to a sink in <= RLE1_OUT_CHUNK byte
// pieces. No heap, no dictionary; state is a few integers plus the output
// chunk. Plain C++ with no Arduino dependency so it can be unit-tested on
// the host (see test/host/rle1_test.cpp).

#include <stddef.h>
#include <stdint.h>

#ifndef RLE1_OUT_CHUNK
#define RLE1_OUT_CHUNK 256
#endif

class Rle1Decoder
{
public:
  // Called with each decoded chunk. Return false to abort decoding.
  typedef bool (*Sink)(const uint8_t *data, size_t len, void *ctx);

  Rle1Decoder(Sink sink, void *ctx, size_t expectedOutput)
      : sink_(sink), ctx_(ctx), expected_(expectedOutput) {}

  // Start over for another plane/stream, reusing the same output chunk.
  void reset(Sink sink, void *ctx, size_t expectedOutput)
  {
    sink_ = sink;
    ctx_ = ctx;
    expected_ = expectedOutput;
    produced_ = 0;
    state_ = TOKEN;
    remaining_ = 0;
    shift_ = 0;
    failed_ = false;
    outLen_ = 0;
  }

  // Feed raw stream bytes. Returns false once the stream is malformed, the
  // output would exceed expectedOutput, or the sink refused data; after that
  // the decoder stays failed.
  bool feed(const uint8_t *data, size_t len)
  {
    if (failed_)
      return false;
    for (size_t i = 0; i < len; i++)
    {
      uint8_t b = data[i];
      switch (state_)
      {
      case TOKEN:
        if (b < 0x80)
        {
          remaining_ = (uint32_t)b + 1;
          state_ = LITERAL;
        }
        else if ((b & 0x7f) == 0x7f)
        {
          remaining_ = 130;
          shift_ = 0;
          state_ = RUN_LEN;
        }
        else
        {
          remaining_ = (uint32_t)(b & 0x7f) + 3;
          state_ = RUN_VALUE;
        }
        break;

      case LITERAL:
        if (!put(b))
          return fail();
        if (--remaining_ == 0)
          state_ = TOKEN;
        break;

      case RUN_LEN:
        if (shift_ > 28)
          return fail(); // varint longer than any sane plane
        remaining_ += (uint32_t)(b & 0x7f) << shift_;
        shift_ += 7;
        if ((b & 0x80) == 0)
          state_ = RUN_VALUE;
        break;

      case RUN_VALUE:
        if (!putRun(b, remaining_))
          return fail();
        state_ = TOKEN;
        break;
      }
    }
    return true;
  }

  // Call after the last feed(). Flushes buffered output and reports whether
  // the stream ended cleanly with exactly expectedOutput bytes decoded.
  bool finish()
  {
    if (failed_)
      return false;
    if (state_ != TOKEN)
      return fail();
    if (!flush())
      return fail();
    return produced_ == expected_;
  }

  size_t produced() const { return produced_; }
  bool failed() const { return failed_; }

private:
  enum State
  {
    TOKEN,
    LITERAL,
    RUN_LEN,
    RUN_VALUE
  };

  bool fail()
  {
    failed_ = true;
    return false;
  }

  bool put(uint8_t b)
  {
    if (produced_ + outLen_ >= expected_)
      return false;
    out_[outLen_++] = b;
    if (outLen_ == sizeof(out_))
      return flush();
    return true;
  }

  bool putRun(uint8_t v, uint32_t len)
  {
    if ((uint64_t)produced_ + outLen_ + len > expected_)
      return false;
    while (len > 0)
    {
      size_t space = sizeof(out_) - outLen_;
      size_t n = len < space ? (size_t)len : space;
      for (size_t k = 0; k < n; k++)
        out_[outLen_ + k] = v;
      outLen_ += n;
      len -= n;
      if (outLen_ == sizeof(out_) && !flush())
        return false;
    }
    return true;
  }

  bool flush()
  {
    if (outLen_ == 0)
      return true;
    size_t n = outLen_;
    outLen_ = 0;
    if (!sink_(out_, n, ctx_))
      return false;
    produced_ += n;
    return true;
  }

  Sink sink_;
  void *ctx_;
  size_t expected_;
  size_t produced_ = 0;
  State state_ = TOKEN;
  uint32_t remaining_ = 0;
  uint8_t shift_ = 0;
  bool failed_ = false;
  uint8_t out_[RLE1_OUT_CHUNK];
  size_t outLen_ = 0;
};
