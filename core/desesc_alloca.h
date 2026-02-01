/*
   ESESC: Super ESCalar simulator
   Copyright (C) 2003 University of Illinois.

   Contributed by Jose Renau

This file is part of ESESC.

ESESC is free software; you can redistribute it and/or modify  it under the terms
of the GNU General Public License as  published by the Free Software Foundation;
either version 2, or (at your option) any later version.

ESESC is    distributed in the  hope that   it will  be  useful, but  WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY  or FITNESS FOR A
PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should  have received a copy of  the GNU General  Public  License along with
ESESC; see the file COPYING.   If not, write to the  Free Software Foundation, 59
Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#pragma once

#if (defined __GNUC__)
#include <cstdlib>
#ifndef alloca
#define alloca __builtin_alloca
#endif
#else
extern void *alloca(uint32_t __size);
#ifndef alloca
#define alloca __builtin_alloca
#endif
#endif /* __GNUC__ */
