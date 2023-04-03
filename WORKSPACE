# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

workspace(name = "desesc")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:git.bzl", "new_git_repository")

# google benchmark
http_archive(
    name = "com_google_benchmark",
    sha256 = "2d6c797dbd21bd230afca4080731c49a71307de0985c63cb69ccde1a41e7148d",
    strip_prefix = "benchmark-b177433f3ee2513b1075140c723d73ab8901790f",
    urls = ["https://github.com/google/benchmark/archive/b177433f3ee2513b1075140c723d73ab8901790f.zip"],
)

# google tests
http_archive(
  name = "com_google_googletest",
  urls = ["https://github.com/google/googletest/archive/609281088cfefc76f9d0ce82e1ff6c30cc3591e5.zip"],
  strip_prefix = "googletest-609281088cfefc76f9d0ce82e1ff6c30cc3591e5",
)

# fmt
http_archive(
    name = "fmt",
    build_file = "fmt.BUILD",
    sha256 = "cdc885473510ae0ea909b5589367f8da784df8b00325c55c7cbbab3058424120",
    strip_prefix = "fmt-9.1.0",
    urls = [
        #"https://github.com/fmtlib/fmt/archive/7bdf0628b1276379886c7f6dda2cef2b3b374f0b.zip",
        "https://github.com/fmtlib/fmt/archive/refs/tags/9.1.0.zip",
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
    sha256 = "06eec38d02f99d225cdad9444102e77d9da717f8cc55f84a3b212abe94a5fc5a",
    strip_prefix = "perfetto-28.0/sdk",
    urls = ["https://github.com/google/perfetto/archive/refs/tags/v28.0.tar.gz"],
)

# Perfetto
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
    sha256 = "93dfc8f4c7d872cc370da70fe831225df959b184ff50bd06f24dbd0936917d2e",
    strip_prefix = "dromajo-c6a1057a23e6a7d8f627938042bc56577a9082ff",
    urls = [
        "https://github.com/masc-ucsc/dromajo/archive/c6a1057a23e6a7d8f627938042bc56577a9082ff.zip",
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
    sha256 = "aafe6907cc422d7619a25ee93e63321eebf95248bef7ba981404a3b70ee10c34",
    strip_prefix = "bazel_clang_tidy-31d62bf825a94468b3d35c5ffd4e014e1c0ff566",
    urls = [
        "https://github.com/erenon/bazel_clang_tidy/archive/31d62bf825a94468b3d35c5ffd4e014e1c0ff566.zip"
    ],
)

