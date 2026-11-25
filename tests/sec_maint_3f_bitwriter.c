// Copyright (c) 2026 guardiancrow. MIT.
#include "src/utils/bit_writer_utils.h"

#include <stdio.h>
#include <string.h>

static int test_capacity(size_t free_words) {
  VP8LBitWriter bw;
  if (!VP8LBitWriterInit(&bw, 64)) return 0;
  const size_t capacity = (size_t)(bw.end - bw.buf);
  const size_t prefix = capacity - free_words * VP8L_WRITER_BYTES;
  memset(bw.buf, 0x5a, capacity);
  bw.cur += prefix;
  bw.bits = 0x04030201u;
  bw.used = VP8L_WRITER_BITS;
  VP8LPutBitsFlushBits(&bw);
  int ok = !bw.error && bw.used == 0 &&
           VP8LBitWriterNumBytes(&bw) == prefix + VP8L_WRITER_BYTES &&
           (bw.cur <= bw.end) &&
           ((size_t)(bw.end - bw.buf) > capacity) == (free_words == 0);
  if (ok) {
    const unsigned char bytes[] = {1, 2, 3, 4};
    ok = VP8L_WRITER_BYTES == sizeof(bytes) &&
         memcmp(bw.buf + prefix, bytes, sizeof(bytes)) == 0;
    for (size_t i = 0; ok && i < prefix; ++i) ok = bw.buf[i] == 0x5a;
    if (ok && free_words > 1) ok = *bw.cur == 0x5a;
  }
  VP8LBitWriterWipeOut(&bw);
  return ok;
}

int main(void) {
  // The initialized writer advances in whole aligned words.
  if (!test_capacity(2) || !test_capacity(1) || !test_capacity(0)) {
    fprintf(stderr, "Candidate libwebp bit writer boundary check failed.\n");
    return 1;
  }
  puts("Candidate BitWriter preserved bytes at spare, exact and full capacity.");
  return 0;
}
