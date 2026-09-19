# Building the i686 Turnip driver

The Mesa source fix is architecture-independent. A separate source patch is
not required for 32-bit guests, but the Turnip DSO must be built for i686 and
placed in the guest root filesystem's 32-bit library directory.

Run this build inside an x86-64 environment with a complete multilib
toolchain and 32-bit development dependencies. On the Volterra test system,
that environment was entered with `FEXBash`; running Meson directly in the
AArch64 host shell would build the wrong architecture.

After applying `patches/0001-freedreno-clamp-view-relative-min-lod.patch`
to the Mesa source tree:

```sh
export PKG_CONFIG_LIBDIR=/usr/lib32/pkgconfig:/usr/share/pkgconfig
export PKG_CONFIG_PATH=

meson setup build-i686 /path/to/mesa \
  --cross-file=/path/to/this-repository/build/i686-cross.ini \
  -Dprefix="$PWD/install-i686" \
  -Dbuildtype=release \
  -Dplatforms=x11,wayland \
  -Dgallium-drivers=[] \
  -Dvulkan-drivers=freedreno \
  -Dfreedreno-kmds=msm \
  -Dllvm=disabled \
  -Dvideo-codecs=[] \
  -Dglx=disabled \
  -Degl=disabled \
  -Dgbm=disabled \
  -Dgles1=disabled \
  -Dgles2=disabled \
  -Dopengl=false \
  -Dshared-glapi=disabled \
  -Dvalgrind=disabled \
  -Dlibunwind=disabled \
  -Dlmsensors=disabled \
  -Dbuild-tests=false

ninja -C build-i686 -j4 \
  src/freedreno/vulkan/libvulkan_freedreno.so
```

Verify the result before staging it:

```sh
file build-i686/src/freedreno/vulkan/libvulkan_freedreno.so
readelf -n build-i686/src/freedreno/vulkan/libvulkan_freedreno.so |
  grep -A1 'Build ID'
sha256sum build-i686/src/freedreno/vulkan/libvulkan_freedreno.so
```

Do not overwrite a live rootfs driver without preserving its hash-identical
stock copy. Keep the 64-bit driver in `usr/lib` and the i686 driver in
`usr/lib32`; replacing one does not patch the other. Confirm the library
actually mapped by the target process before attributing a result to it.

The first i686 build receipt is in
[`../evidence/i686-build.txt`](../evidence/i686-build.txt). It records build
success only. GPU execution and a patched/stock application A/B remain
pending until the Steam/FEX compatibility stack is restored to a coherent
single revision.
