// Host-side tests for src/rle1.h (no Arduino needed).
//
//   test/host/run.sh                 — golden vectors + round trips
//   rle1_test <raw.bin> <enc.rle1>   — decode a stream produced by the server
//                                       encoder and compare with the raw plane
//
// The golden vectors are copied from einktodo/utils/rle-bitmap.test.ts; if
// the format changes, change both.
#include "../../src/rle1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

static int failures = 0;
#define CHECK(cond, ...)                                        \
  do                                                            \
  {                                                             \
    if (!(cond))                                                \
    {                                                           \
      failures++;                                               \
      fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);     \
      fprintf(stderr, __VA_ARGS__);                             \
      fprintf(stderr, "\n");                                    \
    }                                                           \
  } while (0)

// Reference encoder, a straight port of encodeRle1() in the server repo.
static std::vector<uint8_t> encode(const std::vector<uint8_t> &src)
{
  std::vector<uint8_t> out;
  size_t i = 0, n = src.size();
  while (i < n)
  {
    size_t run = 1;
    while (i + run < n && src[i + run] == src[i])
      run++;
    if (run >= 3)
    {
      if (run <= 129)
        out.push_back(0x80 | (uint8_t)(run - 3));
      else
      {
        out.push_back(0xff);
        size_t v = run - 130;
        while (v >= 0x80)
        {
          out.push_back((uint8_t)((v & 0x7f) | 0x80));
          v >>= 7;
        }
        out.push_back((uint8_t)v);
      }
      out.push_back(src[i]);
      i += run;
      continue;
    }
    size_t lit = 0;
    while (i + lit < n && lit < 128)
    {
      size_t p = i + lit;
      if (p + 2 < n && src[p] == src[p + 1] && src[p] == src[p + 2])
        break;
      lit++;
    }
    if (lit == 0)
      lit = 1;
    out.push_back((uint8_t)(lit - 1));
    out.insert(out.end(), src.begin() + i, src.begin() + i + lit);
    i += lit;
  }
  return out;
}

struct Collect
{
  std::vector<uint8_t> data;
  size_t maxChunk = 0;
};

static bool collectSink(const uint8_t *d, size_t len, void *ctx)
{
  Collect *c = (Collect *)ctx;
  c->data.insert(c->data.end(), d, d + len);
  if (len > c->maxChunk)
    c->maxChunk = len;
  return true;
}

// Decode `enc` feeding it in pieces of `chunk` bytes (0 = all at once).
static bool decode(const std::vector<uint8_t> &enc, size_t expected, size_t chunk, Collect &out)
{
  Rle1Decoder dec(collectSink, &out, expected);
  size_t pos = 0;
  while (pos < enc.size())
  {
    size_t n = chunk == 0 ? enc.size() : (chunk < enc.size() - pos ? chunk : enc.size() - pos);
    if (!dec.feed(&enc[pos], n))
      return false;
    pos += n;
  }
  return dec.finish();
}

static void roundTrip(const std::vector<uint8_t> &src, const char *what)
{
  std::vector<uint8_t> enc = encode(src);
  const size_t chunks[] = {0, 1, 7, 128, 1000};
  for (size_t c : chunks)
  {
    Collect out;
    bool ok = decode(enc, src.size(), c, out);
    CHECK(ok, "%s: decode failed (chunk %zu)", what, c);
    CHECK(out.data == src, "%s: mismatch (chunk %zu): got %zu bytes", what, c, out.data.size());
    CHECK(out.maxChunk <= RLE1_OUT_CHUNK, "%s: sink chunk %zu > %d", what, out.maxChunk, RLE1_OUT_CHUNK);
  }
}

static std::vector<uint8_t> bytes(std::initializer_list<int> l)
{
  std::vector<uint8_t> v;
  for (int b : l)
    v.push_back((uint8_t)b);
  return v;
}

static void goldenVectors()
{
  struct
  {
    std::vector<uint8_t> raw, enc;
  } cases[] = {
      {bytes({7}), bytes({0x00, 7})},
      {bytes({7, 7}), bytes({0x01, 7, 7})},
      {bytes({1, 2, 3}), bytes({0x02, 1, 2, 3})},
      {bytes({9, 9, 9}), bytes({0x80, 9})},
      {std::vector<uint8_t>(129, 0xff), bytes({0x80 | 126, 0xff})},
      {std::vector<uint8_t>(130, 0), bytes({0xff, 0x00, 0x00})},
      {std::vector<uint8_t>(130 + 127, 0), bytes({0xff, 0x7f, 0x00})},
      {std::vector<uint8_t>(130 + 128, 0), bytes({0xff, 0x80, 0x01, 0x00})},
      {std::vector<uint8_t>(48000, 0xff), bytes({0xff, 0xfe, 0xf5, 0x02, 0xff})},
      {bytes({1, 2, 0, 0, 0, 0, 3}), bytes({0x01, 1, 2, 0x81, 0, 0x00, 3})},
  };
  int k = 0;
  for (auto &c : cases)
  {
    CHECK(encode(c.raw) == c.enc, "golden %d: reference encoder drifted", k);
    Collect out;
    CHECK(decode(c.enc, c.raw.size(), 1, out) && out.data == c.raw, "golden %d: decoder", k);
    k++;
  }
  // Empty stream.
  Collect out;
  CHECK(decode({}, 0, 0, out) && out.data.empty(), "empty");
}

static void boundaries()
{
  const size_t lens[] = {1, 2, 3, 4, 128, 129, 130, 131, 255, 256, 257, 258, 16383, 16384, 16385, 48000};
  for (size_t len : lens)
  {
    roundTrip(std::vector<uint8_t>(len, 0xa5), "run");
    std::vector<uint8_t> s(len + 4, 0xa5);
    s[0] = 1;
    s[1] = 2;
    s[len + 2] = 3;
    s[len + 3] = 4;
    roundTrip(s, "run between literals");
    std::vector<uint8_t> lit(len);
    for (size_t i = 0; i < len; i++)
      lit[i] = (uint8_t)(i * 37 + 11);
    roundTrip(lit, "literals");
  }
}

static void randomMixes()
{
  uint32_t x = 2463534242u;
  auto rnd = [&]() { x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x; };
  for (int t = 0; t < 200; t++)
  {
    std::vector<uint8_t> s;
    while (s.size() < 3000)
    {
      int kind = rnd() & 3;
      size_t len = 1 + rnd() % (kind == 0 ? 400 : 6);
      uint8_t v = (uint8_t)rnd();
      for (size_t k = 0; k < len; k++)
        s.push_back(kind == 3 ? (uint8_t)rnd() : v);
    }
    roundTrip(s, "random mix");
  }
}

static void badStreams()
{
  std::vector<uint8_t> src(1000, 0);
  for (size_t i = 0; i < src.size(); i += 13)
    src[i] = 0x3c;
  std::vector<uint8_t> enc = encode(src);

  // Cut off mid-stream at every position: must never report success.
  for (size_t cut = 0; cut < enc.size(); cut++)
  {
    Collect out;
    std::vector<uint8_t> part(enc.begin(), enc.begin() + cut);
    CHECK(!decode(part, src.size(), 0, out), "truncated at %zu accepted", cut);
  }
  // Wrong expected length.
  {
    Collect out;
    CHECK(!decode(enc, src.size() - 1, 0, out), "overlong output accepted");
    Collect out2;
    CHECK(!decode(enc, src.size() + 1, 0, out2), "short output accepted");
  }
  // Decoder stays failed after an error.
  {
    Collect out;
    Rle1Decoder dec(collectSink, &out, 3);
    uint8_t s[] = {0x80 | 5, 0x01}; // run of 8 into a 3-byte plane
    CHECK(!dec.feed(s, sizeof(s)), "overflow not detected");
    uint8_t ok[] = {0x80, 0x01};
    CHECK(!dec.feed(ok, sizeof(ok)), "feed after failure succeeded");
    CHECK(!dec.finish(), "finish after failure succeeded");
  }
  // Varint too long.
  {
    Collect out;
    uint8_t s[] = {0xff, 0x80, 0x80, 0x80, 0x80, 0x80, 0x01, 0x00};
    Rle1Decoder dec(collectSink, &out, 1 << 30);
    CHECK(!dec.feed(s, sizeof(s)), "overlong varint accepted");
  }
}

// The firmware keeps one decoder and reset()s it between the black and red
// planes; a reset after a failure must also clear the failed state.
static void resetReuse()
{
  std::vector<uint8_t> a(700, 0x00), b(700, 0xff);
  for (size_t i = 0; i < a.size(); i += 9)
    a[i] = 0x81;
  std::vector<uint8_t> ea = encode(a), eb = encode(b);

  Collect out;
  Rle1Decoder dec(collectSink, &out, a.size());
  CHECK(dec.feed(ea.data(), ea.size()) && dec.finish(), "plane A");
  CHECK(out.data == a, "plane A data");

  Collect out2;
  dec.reset(collectSink, &out2, b.size());
  CHECK(dec.feed(eb.data(), eb.size()) && dec.finish(), "plane B after reset");
  CHECK(out2.data == b, "plane B data");

  // Fail, then reset, then decode fine.
  Collect out3;
  dec.reset(collectSink, &out3, 2);
  uint8_t bad[] = {0x80, 0x01};
  CHECK(!dec.feed(bad, sizeof(bad)), "overflow should fail");
  Collect out4;
  dec.reset(collectSink, &out4, a.size());
  CHECK(dec.feed(ea.data(), ea.size()) && dec.finish() && out4.data == a, "decode after failed+reset");
}

static int fileMode(const char *rawPath, const char *encPath)
{
  FILE *fr = fopen(rawPath, "rb");
  FILE *fe = fopen(encPath, "rb");
  if (!fr || !fe)
  {
    perror("open");
    return 2;
  }
  std::vector<uint8_t> raw, enc;
  uint8_t buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), fr)) > 0)
    raw.insert(raw.end(), buf, buf + n);
  while ((n = fread(buf, 1, sizeof(buf), fe)) > 0)
    enc.insert(enc.end(), buf, buf + n);
  fclose(fr);
  fclose(fe);
  Collect out;
  bool ok = decode(enc, raw.size(), 128, out) && out.data == raw;
  printf("%s: %zu -> %zu bytes (%.1fx) %s\n", encPath, raw.size(), enc.size(),
         enc.empty() ? 0.0 : (double)raw.size() / enc.size(), ok ? "OK" : "MISMATCH");
  return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
  if (argc == 3)
    return fileMode(argv[1], argv[2]);
  goldenVectors();
  boundaries();
  randomMixes();
  badStreams();
  resetReuse();
  if (failures)
  {
    fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  printf("rle1_test: all passed\n");
  return 0;
}
