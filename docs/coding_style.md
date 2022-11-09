# Coding Style

These are the coding style rules for desesc C++. Each rule can be broken, but it
should be VERY rare, and a small comment should be placed explaining why.

Use clang-format with the provided .clang-format configuration.

* When possible keep the system simple. Complexity is the enemy of maintenance.
* Deprecate no longer used features.
* Try to reduce friction. This means to avoid hidden/complex steps.
* Every main API should have a unit test for testing but also to demonstrate usage.

## Naming

* Never camelCase, always use underscore to separate words.
* Use lower case for filenames, variables... The only exceptions:
    * Class/Types: First letter is upper case. E.g: `Config` or `Cluster_manager`
    * constants/constexpr: All upper-case INFINITE, RESET_VALUE
```cpp
foo_bar = Foo_bar(3);
```
* Use plural for containers with multiple entries like vector, singular otherwise
```cpp
elem = entries[index];
```
* Header files end in `.hpp` and C++ files end in `.cpp`.
* Google unit test files end in `_test.cpp`
* Google benchmark files end in `_bench.cpp`

## C++17

desesc is based on esesc which is based on sesc. This means that the code has very old C++
constructs, but the idea is to remove them all. New code should always adhere to the C++17 syntax. Namely:

* Use C++17 iterators when possible
* No malloc/free
* Use std::unique_ptr when it can avoid a new/delete
* OK to pass raw pointer if the method can not allocate/deallocate the object.
* For methods, pass by const ref (`const My_type &`) instead of pointer when possible.
* No preprocessor directives. Only exceptions:
    * `ifndef NDEBUG` for code to use only during debug
    * `include` to include header files
    * `pragma once` as the first directive in header files
* Use enum classes not plain enum.
* If applicable, use structured returns for iterators/funtion returns:
```cpp
for(const auto &[name, id]:name2id) {
  // ...
```
* Use `auto`, or `const auto &`, when possible.
```cpp
for(auto idx:unordered) {
  for(const auto &c:out_edges) {
```
* Use constexpr if possible, const otherwise, or non-const if a non-const variable.

## comments

Code should be the comments, try to keep comments concise. They should explain
the WHY not the HOW. The code is the HOW.

Labels used in comments:

```
// FIXME: Known bug/issue but no time to fix it at the moment

// TODO: Code improvement that will improve perf/quality/??? but no time at the moment

// WARNING: message for some "strange" "weird" code that if changes has effects
// (bug). Usually, this is a "not nice" code that must be kept for some reason.

// NOTE: Any comment that you want to remember something about (not critical)

// STYLE: why you broke a style rule (pointers, iterator...)
```

## strings

Do not use `char *`, use `const std::string &` when possible. Use the
absl::StrCat, absl::StrAppend, absl::StrSplit when possible


## Error handling and exceptions

Use the Config::add_error for any error in the sytem. Once the setup phase is done,
there should be no errors.

```cpp
Config::add_error(fmt::format("ooops in {} due to xxx", variable);
```

## No tabs, indentation is 2 spaces

Make sure to configure your editor to use 2 spaces

You can configure your text editor to do this automatically

## Include order

First do C includes (try to avoid when possible), then an empty line with C++
includes, then an empty line followed with desesc related includes. E.g:

```
#include <sys/types.h>
#include <dirent.h>

#include <iostream>
#include <set>

#include "config.hpp"
#include "iassert.hpp"
```

## Keep column widths short

- Less than 120 characters if at all possible (meaning not compromising
  readability)

You can configure your text editor to do this automatically

## Avoid trailing spaces

You can configure your text editor to highlight them.
 https://github.com/ntpeters/vim-better-whitespace

## Do not use std::unordered_set, std::map, use flat_hash_map or flat_hash_set from abseil

```cpp
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

absl::flat_hash_map<Index_ID, RTLIL::Wire *>   my_example;
```

## Some common idioms to handle map/sets

Traverse the map/set, and as it traverses decide to erase some of the entries:
```cpp
for (auto it = m.begin(), end = m.end(); it != end;) {
  if (condition_to_erase_it) {
    m.erase(it++);
  } else {
    ++it;
  }
}
```

To check if a key is present:
```cpp
if (set.contains(key_value)) {
}
```

## Avoid dynamic allocation as much as possible

The idea is to RARELY directly allocate pointer allocation. esesc did not have
any dynamic allocation after the setup phase is done. Only pools did.

Use:

```cpp
foo = Sweet_potato(3, 7)
```

instead of

```cpp
foo = new Sweet_potato(3, 7)
```

## Do not use "new"/"delete" keywords. Use smart pointers if needed (VERY VERY rare)


Use:
```cpp
foo = std::make_unique<Sweet_potato>(3,7);
```

instead of

```cpp
foo = new Sweet_potato(3, 7)
```


## Use fmt::print to print messages for debugging

```cpp
fmt::print("This is a debug message, name = {}, id = {}\n",g->get_name(), idx);
```

## Use accessors consistently

* get_XX(): gets "const XX &" from object without side effects (assert if it does not exist)
    * operator(Y) is an alias for get_XX(Y)
* ref_XX(): gets "XX * " (nullptr if it does not exist)
* find_XX(): similar to get_XX but, if it does not exist return invalid object (is_invalid())
* setup_XX(): gets XX from object, if it does not exists, it creates it
* create_XX(): clears previous XX from object, and creates a new and returns it
* set_XX(): sets XX to object, it creates if it does not exist. Similar to
  create, but does not return reference.

If a variable is const, it can be exposed directly without get/set accessors

foo = x.const_var;  // No need to have x.get_const_var()

## Use iassert extensively / be meaningful whenever possible in assertions

This usually means use meaningful variable names and conditions that are easy to understand.
If the meaning is not clear from the assertion, use a comment in the same line.
This way, when the assertion is triggered it is easy to identify the problem.

```cpp
I(n_edges > 0); //at least one edge needed to perform this function
```

We use the https://github.com/masc-ucsc/iassert package. Go to the iassert for more details on the advantages
and how to allow it to use GDB with assertions.

## Develop in debug mode and benchmark in release mode

Extra checks should be only in debug. Debug and release must execute the same,
only checks (not behavior change) allowed in debug mode.

Benchmark in release. It is 2x-10x faster.

## Use compact if/else brackets

Use clang-format as configured to catch style errors. desesc clang-format is
based on google format, but it adds several alignment directives and wider
terminal.

```
   cd XXXX
   clang-format -i *pp
```

```cpp
  if (opack.graph_name != "") {
     // ...
  } else {
     // ..
  }
```

## Avoid code duplication

The rule is that if the same code appears in 3 places, it should be refactored

Tool to detect duplication

```
find . -name '*.?pp' | grep -v test >list.txt
duplo -ml 12 -pt 90 list.txt report.txt
```

## Functions

Functions  should be short  and sweet,  and do  just one  thing.  They
should fit on  one or two screenfuls of text  (for a 124x50 terminal),
and do one thing and do that  well. (PLEASE NO MORE THAN 100 lines per
function)

Use  helper functions  with
descriptive names  (you can  ask the compiler  to in-line them  if you
think it's performance-critical, and it  will probably do a better job
of it that you would have done).

Another  measure of  the function  is the  number of  local variables.
They shouldn't exceed 5-10, or you're doing something wrong.  Re-think
the function,  and split  it into smaller  pieces.  A human  brain can
generally easily keep track of about 7 different things, anything more
and it gets confused.  You know you're brilliant, but maybe you'd like
to understand what you did 2 weeks from now.

