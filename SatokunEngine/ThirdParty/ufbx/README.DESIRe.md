# ufbx in DESI:Re

- Upstream: https://github.com/ufbx/ufbx
- Release: v0.23.0
- Pinned commit: `fcc5d6ba444cfd3eb80677dba5e37e493941abe5`
- License selected by this project: **Alternative A — MIT**.
- `ufbx.h`, `ufbx.c`, and `LICENSE` are unmodified upstream files. Keep the copyright and license text in distributions.
- `UfbxImplementation.cpp` includes `ufbx.c` exactly once. Do not compile the `.c` separately.

SHA-256:

```text
ufbx.h   942481725372d2ac4da5e77a062b47c20054a3440e7ee09a6043f99fe1f130ed
ufbx.c   7d8d6ae4373f71692f295ff49ee0826466306ebcaa80b0e587c13ed047b98cea
LICENSE  0dd48ebadf52273c736256325c8f078c03c8bb4facee22a4122de0ad3f615391
```

FBX parsing and animation baking are provided by ufbx. DESI:Re's ModelLoader, Animator, and GPU skinning connect that library to the engine; they are not part of the upstream library.
