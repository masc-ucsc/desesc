
#pragma once

#include <stdint.h>
#include <stdio.h>

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "fmt/format.h"
#include "iassert.hpp"

using namespace std;

class DrawArch {
protected:
  std::vector<std::string> rows;

public:
  DrawArch() {
    // Constructor
  };
  ~DrawArch() {}

  void addObj(const std::string &mystr) { rows.push_back(mystr); }

  void drawArchDot(const std::string &filename) {
    std::fstream fs(filename, std::fstream::out);
    if (!fs.good()) {
      std::cerr << "WriteFile() : Opening " << filename << " file failed." << std::endl;
      return;
    }

    fs << "\ndigraph simple_hierarchy {";
    fs << "\n\nnode [color=Green,fontcolor=Blue,font=Courier,shape=record]\n";

    std::string str;
    for (size_t i = 0; i < rows.size(); i++) {
      str = rows.at(i);
      fs << "\n";
      fs << str.c_str();
      // if (i < (rows.size() - 1))
      //  fs << ",";
    }

    fs << "\n}\n";
  }
};
