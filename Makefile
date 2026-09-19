GLSLANG_VALIDATOR ?= glslangValidator
CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra

.PHONY: all clean probe

all: reproducer/repro reproducer/lod-pack-probe

reproducer/producer.spv: reproducer/producer.comp
	$(GLSLANG_VALIDATOR) -V $< -o $@

reproducer/fetch.spv: reproducer/fetch.comp
	$(GLSLANG_VALIDATOR) -V $< -o $@

reproducer/repro: reproducer/repro.c reproducer/producer.spv reproducer/fetch.spv
	$(CC) $(CFLAGS) $< -o $@ -lvulkan -lm

reproducer/lod-pack-probe: reproducer/lod_pack_probe.c
	$(CC) $(CFLAGS) $< -o $@ -lm

probe: reproducer/lod-pack-probe
	./reproducer/lod-pack-probe -4

clean:
	rm -f reproducer/producer.spv reproducer/fetch.spv
	rm -f reproducer/repro reproducer/lod-pack-probe
