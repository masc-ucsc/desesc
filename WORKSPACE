# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

workspace(name = "desesc")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:git.bzl", "new_git_repository")

http_archive(
    name = "rules_foreign_cc",
    sha256 = "c905d5ba97d102153b7b8cacc8fa1f1c29623a710264c992cd2cddcb9d616527",
    strip_prefix = "rules_foreign_cc-0.10.1",
    url = "https://github.com/bazelbuild/rules_foreign_cc/archive/0.10.1.zip",
)

load("@rules_foreign_cc//foreign_cc:repositories.bzl", "rules_foreign_cc_dependencies")

rules_foreign_cc_dependencies()

# google benchmark
http_archive(
    name = "com_google_benchmark",
    sha256 = "abfc22e33e3594d0edf8eaddaf4d84a2ffc491ad74b6a7edc6e7a608f690e691",
    strip_prefix = "benchmark-1.8.3",
    urls = ["https://github.com/google/benchmark/archive/refs/tags/v1.8.3.zip"],
)

# google tests
http_archive(
  name = "com_google_googletest",
  sha256 = "ffa17fbc5953900994e2deec164bb8949879ea09b411e07f215bfbb1f87f4632",
  urls = ["https://github.com/google/googletest/archive/refs/tags/v1.13.0.zip"],
  strip_prefix = "googletest-1.13.0",
)

# fmt
http_archive(
    name = "fmt",
    build_file = "fmt.BUILD",
    sha256 = "3c2e73019178ad72b0614a3124f25de454b9ca3a1afe81d5447b8d3cbdb6d322",
    strip_prefix = "fmt-10.1.1",
    urls = [
        "https://github.com/fmtlib/fmt/archive/refs/tags/10.1.1.zip",
    ],
)

# iassert
http_archive(
    name = "iassert",
    sha256 = "c6bf66a76d5a1de57c45dba137c9b51ab3b4f3a31e5de9e3c3496d7d36a128f8",
    strip_prefix = "iassert-5c18eb082262532f621a23023f092f4119a44968",
    urls = [
        "https://github.com/masc-ucsc/iassert/archive/5c18eb082262532f621a23023f092f4119a44968.zip",
    ],
)

# abseil
http_archive(
  name = "com_google_absl",
  strip_prefix = "abseil-cpp-20211102.0",
  urls = ["https://github.com/abseil/abseil-cpp/archive/refs/tags/20211102.0.zip"],
  sha256 = "a4567ff02faca671b95e31d315bab18b42b6c6f1a60e91c6ea84e5a2142112c2",
)

# Perfetto
http_archive(
    name = "com_google_perfetto",
    build_file = "perfetto.BUILD",
    sha256 = "bd78f0165e66026c31c8c39221ed2863697a8bba5cd39b12e4b43d0b7f71626f",
    strip_prefix = "perfetto-40.0/sdk",
    urls = ["https://github.com/google/perfetto/archive/refs/tags/v40.0.tar.gz"],
)

# toml11
http_archive(
    name = "toml11",
    build_file = "toml11.BUILD",
    sha256 = "4124577f989d6a558229ef8f06944ca210e4cf1fe72975eaa2528f1a53f129c4",
    strip_prefix = "toml11-3.7.1",
    urls = [
        "https://github.com/ToruNiina/toml11/archive/v3.7.1.zip",
    ],
)

# Dromajo
http_archive(
    name = "dromajo",
    build_file = "dromajo.BUILD",
    patches = ["//external:dromajo.patch"],
    sha256 = "351a6cba0d42eb0b3f49289376f6af3ca313aaa6718ed7fcb43f0f3a4b5a2d48",
    strip_prefix = "dromajo-866b264a093551ff5442f241d90bea9ac7eb3431",
    urls = [
        "https://github.com/masc-ucsc/dromajo/archive/866b264a093551ff5442f241d90bea9ac7eb3431.zip",
    ],
)

# libelf (needed by dromajo in OSX)
http_archive(
    name = "libelf",
    build_file = "libelf.BUILD",
    sha256 = "c0627b45c29a151e4e1105988ad7ce9bf83b52cbbca0a1db06c7fcad69b85c4b",
    strip_prefix = "libelf-ba3c81450b91d1935fff01bae191a59d7653d2a5",
    urls = [
        "https://github.com/masc-ucsc/libelf/archive/ba3c81450b91d1935fff01bae191a59d7653d2a5.zip",
    ],
)

# clang-tidy
http_archive(
    name = "bazel_clang_tidy",
    sha256 = "6de1b53a3b011b8bfc27d6e5e8edf9f31b9c08beeb92d6edd348c7680861e99a",
    strip_prefix = "bazel_clang_tidy-d2aecc583d14c9554febeab185833c1e8cce5384",
    urls = [
        "https://github.com/erenon/bazel_clang_tidy/archive/d2aecc583d14c9554febeab185833c1e8cce5384.zip"
    ],
)

