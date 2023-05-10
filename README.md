# desesc

Dromajo ESESC

DESESC is an evolution from SESC, but it does not use semantic versioning. The
reason is that after each major change (new letter) there is a period of "beta"
unstable which is akind of 0.x version in semantic versioning.

Each major release adds a letter:

[SESC](https://sesc.sourceforge.net) 

 MIPS based OoO core with multicore and TLS support

[ESESC](https://masc.soe.ucsc.edu/esesc/)

Major changes from SESC:

  * QEMU based emulator (MIPS/RISC-V/ARM)
  * Power/thermal
  * Statistical sampling

DESESC:

Major changes from ESESC:

  * Dromajo instead of QEMU
  * Specter Safe OoO model
  * Major rework for modern C++
  * New configuration

## Setup

DESESC requires gcc (or clang) versions 10 or newer and bazelisk.

### Install bazelisk

**Install Bazel**isk

Bazelisk is a wrapper around bazel that allows you to use a specific version.

If you do not have system permissions, you can install a local bazelisk

```sh
npm install  @bazel/bazelisk
alias bazel=$(pwd)/node_modules/\@bazel/bazelisk/bazelisk.js
```

You can also install it directly if you have administrative permissions:

macos:
```sh
brew install bazelisk.
```

Linux:
```sh
go install github.com/bazelbuild/bazelisk@latest
export PATH=$PATH:$(go env GOPATH)/bin
```

Arch linux:
```sh
pacaur -S bazelisk  # or yay or paru installers
```

### Dhrystone example

To build `desesc` with debug version (debug/develop)
```
bazel build -c dbg //main:desesc
```

To build `desesc` with release version (long simulations)
```
bazel build -c dbg //main:desesc
```

To run the default simulation (dhrystone):
```
./bazel-bin/main/desesc -c ./conf/desesc.toml
```
