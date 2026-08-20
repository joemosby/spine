# Spine

Phase 0 workspace for a mixed-criticality runtime.

This repository is a Bzlmod-only landing area. `@spine//runtime` is the
mailbox + loop + observation library. It is not a scheduler. Long form
for the contract lives in `docs/`. This README does not claim isolation
or a measured deadline.

## What this tree is

A Bzlmod-only Bazel skeleton, portable as a third-party module:

- Bazel 9.2.0, pinned with bazelisk via `.bazelversion`
- Module name `spine`. Consumers use `bazel_dep(name = "spine")` and depend
  on `@spine//runtime`. No WORKSPACE consumers.
- Dependencies from the Bazel Central Registry (`rules_cc`, `platforms`,
  and `toolchains_llvm` as a root-only `dev_dependency`)
- A docs `//:docs` target and the public `//runtime` library

## What this tree is not

This README does not claim isolation, spatial safety, deadlines, measured
timing, or certification. Those are out of scope for Phase 0 docs.

There is no `cc_binary` and no sample app. `@spine//runtime` is the
mailbox + loop + observation `cc_library`. It is not a scheduler. `T`
and `N` are caller-supplied init config, not a measured deadline.

Isolation, when a package exists, is Linux-only and is not required to use
the mailbox/loop. It is not a dep of `@spine//runtime`. Harness is not a
public dep. This README does not claim isolation is implemented.

## Toolchain (honest)

`rules_cc` does not ship a hermetic toolchain. For **this repository's**
own builds, `MODULE.bazel` registers `toolchains_llvm` 1.8.0 with LLVM
19.1.7 as a root-module `dev_dependency` and `.bazelrc` disables host C++
autoconfig (`BAZEL_DO_NOT_DETECT_CPP_TOOLCHAIN=1`).

That means:

- In this workspace, the compiler is a Bazel-fetched LLVM distribution, not
  whatever `gcc` or `clang` happens to be on `PATH`.
- This is **not** a fully hermetic C++ environment. Native compiles still
  use the host sysroot (libc and system headers) unless a committed sysroot
  is added later. Phase 0 does not add one.
- A consumer of `spine` does **not** inherit that fetched LLVM or this
  sysroot. They bring the toolchain.

## Build

Requires [bazelisk](https://github.com/bazelbuild/bazelisk). Then:

```bash
bazelisk build //...
```

That builds the docs package graph and `@spine//runtime`. It does not
claim isolation or a measured deadline.

```bash
bazelisk build //runtime:runtime
bazelisk test //runtime:runtime_test
```
