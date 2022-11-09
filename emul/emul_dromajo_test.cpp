#include "emul_dromajo.hpp"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <fstream>
#include <getopt.h>
#include <inttypes.h>
#include <net/if.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

//#define REGRESS_COSIM 1
#ifdef REGRESS_COSIM
#include "dromajo_cosim.h"
#endif

int main(int argc, char **argv) {
    std::ofstream file;
    file.open("emul_dromajo_test.toml");

    file << "[emul]\n";
    file << "num = \"1\"\n";
    file << "type = \"dromajo\"\n";
    file.close();

    Emul_dromajo dromajo_emul("emul_dromajo_test.toml");

    if (dromajo_emul.init_dromajo_machine(argc, argv))
        fmt::print("dromajo init sucess\n");
    else
        return 0;

    for (int i = 0; i < 15; i++) {
        Dinst *new_dinst = dromajo_emul.peek(0);
        fmt::print("pc executed {:#016x}\n",new_dinst->getPC());
	dromajo_emul.execute(0);
    }

    return 1;
}
