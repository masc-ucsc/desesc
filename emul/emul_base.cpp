// See LICENSE for details.

#include "emul_base.hpp"

//#include "config.hpp"

Emul_base::Emul_base(Config conf)
    : config(conf)
/* Base class Emul_base constructor  */
{
  num  = Config::get_integer("emul", "num");
  type = Config::get_string("emul", "type", {"dromajo", "trace"});

  fmt::print("emul.num={} emul.type={}\n", num, type);
}

Emul_base::~Emul_base()
/* Emul_base destructor  */
{}
