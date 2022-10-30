
#pragma once

#include <fstream>
#include <iostream>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>

#include "iassert.hpp"

using namespace std;

class DrawArch {
protected:
  std::vector<std::string> rows;

public:
  DrawArch(){
      // Constructor
  };
  ~DrawArch(){
  }

  void addObj(const std::string &mystr) {
    rows.push_back(mystr);
  }

  void drawArchDot(const char *filename) {
    if(filename == NULL) {
      fmt::print("Invalid (NULL) file name. Cannot generate the architecture file\n");
      return;
    }

    std::fstream fs(filename, std::fstream::out);
    if(!fs.good()) {
      std::cerr << "WriteFile() : Opening file failed." << std::endl;
      return;
    }

    fs << "\ndigraph simple_hierarchy {";
    fs << "\n\nnode [color=Green,fontcolor=Blue,font=Courier,shape=record]\n";

    std::string str;
    for(size_t i = 0; i < rows.size(); i++) {
      str = rows.at(i);
      fs << "\n";
      fs << str.c_str();
      // if (i < (rows.size() - 1))
      //  fs << ",";
    }

    fs << "\n}\n";
  }
};

