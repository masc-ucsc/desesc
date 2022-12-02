// See LICENSE for details.

#pragma once

#include <string>

class Report {
private:
  static inline int fd = -1;

public:
  static void init();
  static void reinit();
  static void field(const std::string &msg);
  static void close();

  static int raw_file_descriptor() { return fd; }
};
