// See LICENSE for details.

#include <unistd.h>
#include <fcntl.h>

#include "absl/strings/str_cat.h"

#include "config.hpp"
#include "iassert.hpp"
#include "report.hpp"

void Report::init() {

  std::string report_file = "desesc";

  const char *rep1 = getenv("REPORTFILE");
  if(rep1) {
    absl::StrAppend(&report_file, "_", rep1);
  }
  const char *rep2 = getenv("REPORTFILE2");
  if(rep2) {
    absl::StrAppend(&report_file, "_", rep2);
  }

  absl::StrAppend(&report_file, ".XXXXXX");

  char f[report_file.size()+10];
  strcpy(f,report_file.c_str());
  fd    = ::mkstemp(f);
  if (fd == -1) {
    perror("Report::init could not assign file name:");
    exit(-1);
  }
}

void Report::reinit() {
  if (fd>=0)
    close();

  init();
}

void Report::close() {
  ::close(fd);
}

void Report::field(const std::string &msg) {
  write(fd, msg.data(), msg.size());
  if (msg.back() != '\n') {
    write(fd,"\n",1);
  }
}

