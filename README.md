# Spine

Phase 0 workspace for a mixed-criticality runtime.

This repository is the build-system and landing area. It does not yet contain
runtime code. The inter-domain contract is owned by Runtime and will land
separately after Architect stamp. Do not treat this tree as that contract.

## What this tree is

A Bzlmod-only Bazel skeleton, portable as a third-party module:

- Bazel 9.2.0, pinned with bazelisk via `.bazelversion`
- Module name `spine`. Consumers use `bazel_dep(name = "spine")` and depend
  on `@spine//runtime`. No WORKSPACE consumers.
- Dependencies from the Bazel Central Registry (`rules_cc`, `platforms`,
  and `toolchains_llvm` as a root-only `dev_dependency`)
- A docs `//:docs` target and a public placeholder `//runtime`

## What this tree is not

This README does not claim isolation, spatial safety, deadlines, measured
timing, or certification. Those are out of scope for Phase 0 docs.

There is no `cc_binary`, no RT loop, and no sample app. `@spine//runtime`
is a public placeholder `cc_library` (header only). Runtime owns the
inter-domain contract and the first C++ that implements it.

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

That builds the docs package graph and the public `@spine//runtime`
placeholder. It does not compile an RT loop or product C++.
