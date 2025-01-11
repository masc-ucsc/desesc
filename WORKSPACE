# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

workspace(name = "desesc")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:git.bzl", "new_git_repository")

http_archive(
    name = "rules_foreign_cc",
    sha256 = "952fb22638d608f8eb9dc6b905e92641469d50333b5618cc367ee6bde6c6f011",
    strip_prefix = "rules_foreign_cc-0.13.0",
    url = "https://github.com/bazelbuild/rules_foreign_cc/archive/0.13.0.zip",
)

load("@rules_foreign_cc//foreign_cc:repositories.bzl", "rules_foreign_cc_dependencies")

rules_foreign_cc_dependencies()

# google benchmark
http_archive(
    name = "com_google_benchmark",
    sha256 = "8a63c9c6adf9e7ce8d0d81f251c47de83efb5e077e147d109fa2045daac8368b",
    strip_prefix = "benchmark-1.9.1",
    urls = ["https://github.com/google/benchmark/archive/refs/tags/v1.9.1.zip"],
)

# google tests
http_archive(
  name = "com_google_googletest",
  sha256 = "f179ec217f9b3b3f3c6e8b02d3e7eda997b49e4ce26d6b235c9053bec9c0bf9f",
  urls = ["https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip"],
  strip_prefix = "googletest-1.15.2",
)

# fmt
http_archive(
    name = "fmt",
    build_file = "fmt.BUILD",
    sha256 = "7aa4b58e361de10b8e5d7b6c18aebd98be1886ab3efe43e368527a75cd504ae4",
    strip_prefix = "fmt-11.0.2",
    urls = [
        "https://github.com/fmtlib/fmt/archive/refs/tags/11.0.2.zip",
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
# http_archive(
#   name = "com_google_absl",
#   strip_prefix = "abseil-cpp-20240116.2",
#   urls = ["https://github.com/abseil/abseil-cpp/archive/refs/tags/20240116.2.zip"],
#   sha256 = "69909dd729932cbbabb9eeaff56179e8d124515f5d3ac906663d573d700b4c7d",
# )

# Perfetto
http_archive(
    name = "com_google_perfetto",
    build_file = "perfetto.BUILD",
    sha256 = "8d1c6bf44f1bdb098ab70cd60da3ce6b6e731e4eb21dd52b2527cbdcf85d984d",
    strip_prefix = "perfetto-48.1/sdk",
    urls = ["https://github.com/google/perfetto/archive/refs/tags/v48.1.tar.gz"],
)

# toml11
http_archive(
    name = "toml11",
    build_file = "toml11.BUILD",
    sha256 = "e3f956df9e2c162cddf7e4ef83f35de9e2771b8e6997faa2bb3ceae5ee9c2757",
    strip_prefix = "toml11-4.2.0",
    urls = [
        "https://github.com/ToruNiina/toml11/archive/v4.2.0.zip",
    ],
)

# Dromajo
http_archive(
    name = "dromajo",
    build_file = "dromajo.BUILD",
    patches = ["//external:dromajo.patch"],
    sha256 = "25af8ca914f0ca3f41a6250fa6069621e3fcaa3230e1bc95ff813eaec9c7e3dd",
    strip_prefix = "dromajo-2a9f5bbfbefa92bc0d81bb83e44e6bab963302cb",
    urls = [
      "https://github.com/masc-ucsc/dromajo/archive/2a9f5bbfbefa92bc0d81bb83e44e6bab963302cb.zip",
    ],
)

# superbp
http_archive(
    name = "superbp",
    sha256 = "735d1031d4e92d4b570cd4b9dccbfed3e6e405aee7d3712f65ece214de42a280",
    strip_prefix = "superbp-e1d60cd291ab1f1af1eb0f8c823f92d8790d5526",
    urls = [
      "https://github.com/masc-ucsc/superbp/archive/e1d60cd291ab1f1af1eb0f8c823f92d8790d5526.zip",
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
    sha256 = "e1440a34d7abb720005d7ba1db4b50fbe93850fbb88c0e28f8865f35dd936245",
    strip_prefix = "bazel_clang_tidy-f23d924918c581c68cd5cda5f12b4f8198ac0c35",
    urls = [
        "https://github.com/erenon/bazel_clang_tidy/archive/f23d924918c581c68cd5cda5f12b4f8198ac0c35.zip"
    ],
)

