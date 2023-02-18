// See LICENSE for details.

#include "gxbar.hpp"

#include "config.hpp"
#include "memory_system.hpp"

GXBar::GXBar(const std::string &section, const std::string &name)
    /* constructor {{{1 */
    : MemObj(section, name) {}
