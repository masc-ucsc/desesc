// See LICENSE for details.

#include "emul_base.hpp"

// #include "config.hpp"

Emul_base::Emul_base(Config conf)
    : config(conf)
/* Base class Emul_base constructor  */
{
  num  = 0;
  type = "INVALID";
  const std::vector<std::string> emu_list = {"drom_emu"};
  fmt::print("get string = {} \n", Config::get_string("soc", "emul", (size_t)0, emu_list));
  if (Config::get_string("soc", "emul") == "drom_emul") {
    num    = Config::get_integer("drom_emu", "num");
    type   = Config::get_string("drom_emu", "type", {"dromajo", "trace"});
    rabbit = Config::get_integer("drom_emu", "rabbit");
    detail = Config::get_integer("drom_emu", "detail");
    time   = Config::get_integer("drom_emu", "time");
  }
  fmt::print("num={} type={} rabbit={} detail={} time={}\n", num, type, rabbit, detail, time);
}

Emul_base::~Emul_base()
/* Emul_base destructor  */
{}
