# Mesa upstreaming checklist

This repository preserves technical evidence. It is not the branch that
should be submitted to Mesa.

Before opening a Mesa merge request:

1. Create a personal fork of `mesa/mesa` on freedesktop.org GitLab.
2. Create a clean branch from current upstream `main` in a full Mesa checkout.
3. Apply only the source change from `patches/` and verify that each commit is
   buildable and bisectable.
4. Confirm the patch follows Mesa coding conventions and does not mix
   formatting with the functional change.
5. Run the applicable Mesa test suite with `meson test`, plus relevant dEQP
   coverage where available. Record exact commands and results.
6. Run the standalone Vulkan reproducer on stock, patched, and reverted
   drivers. Record the exact Mesa revision, architecture, compiler, GPU, and
   validation-layer result.
7. Inspect the history of the affected file and use the component prefix that
   current maintainers use. Keep every commit-message line at 75 characters
   or fewer.
8. If identifying the introducing commit, use Mesa's `Fixes:` trailer format.
   Use a full `Closes:` URL for a related GitLab issue; do not use `Fixes:` for
   issue links.
9. Add testing, review, and stable-backport trailers only when they are earned
   and accurate.
10. Enable “Allow commits from members who can merge to the target branch.”
11. Keep a clean history when responding to review; squash fixups before each
    update.

Mesa's current policy also requires direct human oversight of AI-assisted
work. The submitter must understand the change, take responsibility for its
licensing and correctness, write the upstream commit message and merge-request
discussion in their own words, and disclose assistance as required. Do not
copy the historical issue draft as an upstream submission without personally
rewriting and validating it.

Relevant upstream references:

- [Submitting patches](https://docs.mesa3d.org/submittingpatches.html)
- [Mesa coding style](https://docs.mesa3d.org/codingstyle.html)
- [Mesa source repository](https://gitlab.freedesktop.org/mesa/mesa)

The likely introducing commit found by source history is
`db69218cbef4c4d59b87d98c0562ee28e815b00f` (`tu: Implement
VK_EXT_image_view_min_lod`). Verify that conclusion against the exact upstream
branch before using it in a `Fixes:` trailer.
