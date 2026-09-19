# Patch

`0001-freedreno-clamp-view-relative-min-lod.patch` is a plain unified diff
against Mesa `main` commit
`590bf21d918c86908d96d1f4590ecd25b9657171` (2026-09-17).

Check or apply it from a Mesa checkout with:

```sh
git apply --check /path/to/0001-freedreno-clamp-view-relative-min-lod.patch
git apply /path/to/0001-freedreno-clamp-view-relative-min-lod.patch
```

The patch intentionally contains only the A6xx/A7xx change validated on
Adreno 690. The analogous A8xx expression should be evaluated separately.

For an upstream Mesa merge request, apply the change to a clean branch of the
current Mesa tree and create the commit yourself in accordance with Mesa's
submission policy. This file is not an email-formatted commit and contains no
prewritten upstream commit message.
