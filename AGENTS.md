# Agent notes

This workspace is not fully hermetic.

The compiler is a Bazel-fetched LLVM. Native links still use the host
sysroot. Do not write "hermetic" as a claim.

Do not claim isolation, deadlines, or certification.
