/*
 * CPU-only diagnostic for the Turnip MIN_LOD_CLAMP conversion.
 *
 * The original operation deliberately includes an out-of-range
 * floating-to-unsigned conversion. Its output is not guaranteed by C or C++.
 * Run it in a separate process and do not use the original operation in
 * production code. Input is read at runtime to avoid constant folding.
 *
 * Build: cc -O2 -Wall -Wextra lod_pack_probe.c -o lod-pack-probe
 * Run:   ./lod-pack-probe -4
 * UBSan: cc -O2 -fsanitize=float-cast-overflow lod_pack_probe.c \
 *           -o lod-pack-ubsan
 *        ./lod-pack-ubsan -4
 */

typedef unsigned int u32;
#if defined(__cplusplus)
static_assert(sizeof(u32) == 4, "32-bit unsigned int required");
#else
_Static_assert(sizeof(u32) == 4, "32-bit unsigned int required");
#endif

#if defined(__GNUC__) || defined(__clang__)
#define NOINLINE __attribute__((noinline))
#else
#define NOINLINE
#endif

/* Deliberately preserves the affected out-of-range conversion. */
NOINLINE u32
pack_original(float relative_min_lod)
{
   return ((u32)(relative_min_lod * 256.0)) & 0xfffu;
}

/* Candidate lower-bound correction, not a complete driver patch. */
NOINLINE u32
pack_clamped(float relative_min_lod)
{
   const float nonnegative = relative_min_lod > 0.0f ? relative_min_lod : 0.0f;
   return ((u32)(nonnegative * 256.0)) & 0xfffu;
}

#ifndef PACK_ONLY
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

int
main(int argc, char **argv)
{
   if (argc != 2) {
      fprintf(stderr, "Usage: %s RELATIVE_MIN_LOD (e.g. -4)\n", argv[0]);
      return 2;
   }

   char *end = NULL;
   errno = 0;
   const float lod = strtof(argv[1], &end);
   if (errno || end == argv[1] || *end != '\0' || !isfinite(lod) ||
       lod < -16.0f || lod > 15.99609375f) {
      fprintf(stderr, "Expected a finite value in [-16, 15.99609375].\n");
      return 2;
   }

   const u32 original = pack_original(lod);
   const u32 clamped = pack_clamped(lod);
   printf("relative_min_lod: %.8g\n", (double)lod);
   printf("original: field=0x%03x, decoded_lod=%.8g\n", original,
          original / 256.0);
   printf("clamped:  field=0x%03x, decoded_lod=%.8g\n", clamped,
          clamped / 256.0);
   return 0;
}
#endif
