// See LICENSE for details.

#include "emul_base.hpp"

// #include "config.hpp"

Emul_base::Emul_base(Config conf)
    : config(conf)
/* Base class Emul_base constructor  */
{
  /* add possible drivers to this list */
  const std::vector<std::string> emu_list = {"dromajo", "trace"};

  type   = Config::get_string("soc", "emul", 0, "type", emu_list);
  num    = Config::get_integer("soc", "emul", 0, "num");
  rabbit = Config::get_integer("soc", "emul", 0, "rabbit");
  detail = Config::get_integer("soc", "emul", 0, "detail");
  time   = Config::get_integer("soc", "emul", 0, "time");

  fmt::print("num={} type={} rabbit={} detail={} time={}\n", num, type, rabbit, detail, time);
}

Emul_base::~Emul_base()
/* Emul_base destructor  */
{}
