# Spine

Phase 0 workspace for a mixed-criticality runtime.

This repository is the build-system and landing area. It does not yet contain
runtime code. The inter-domain contract is owned by Runtime and will land
separately after Architect stamp. Do not treat this tree as that contract.

## What this tree is

A Bzlmod-only Bazel skeleton:

- Bazel 9.2.0, pinned with bazelisk via `.bazelversion`
- Dependencies from the Bazel Central Registry (`rules_cc`, `platforms`,
  `toolchains_llvm`)
- A docs-only `//:docs` target so the package graph resolves

## What this tree is not

This README does not claim isolation, spatial safety, deadlines, measured
timing, or certification. Those are out of scope for Phase 0 docs.

There is no `cc_binary`, no RT loop, and no sample app. Runtime owns the
inter-domain contract and the first C++ that implements it.

## Toolchain (honest)

`rules_cc` does not ship a hermetic toolchain. This workspace registers
`toolchains_llvm` 1.8.0 with LLVM 19.1.7 and disables host C++ autoconfig
(`BAZEL_DO_NOT_DETECT_CPP_TOOLCHAIN=1`).

That means:

- The compiler is a Bazel-fetched LLVM distribution, not whatever `gcc` or
  `clang` happens to be on `PATH`.
- This is **not** a fully hermetic C++ environment. Native compiles still
  use the host sysroot (libc and system headers) unless a committed sysroot
  is added later. Phase 0 does not add one.

## Build

Requires [bazelisk](https://github.com/bazelbuild/bazelisk). Then:

```bash
bazelisk build //...
```

That builds the docs package graph. It does not compile C++.
