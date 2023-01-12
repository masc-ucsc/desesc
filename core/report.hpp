// See LICENSE for details.

#pragma once

#include <string>

class Report {
private:
  static inline std::string report_file;
  static inline int fd = -1;

public:
  static void init();
  static void reinit();
  static void field(const std::string &msg);
  static void close();

  static const std::string get_extension();

  static int raw_file_descriptor() { return fd; }
};
