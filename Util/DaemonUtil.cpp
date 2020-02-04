#include <cstdio>
#include "DaemonUtil.hpp"

namespace AlohaIO
{

void PrintUsage(const char *argv0)
{
    fprintf(stderr, "USAGE:\n\t%s <conf_file> [nofork]\n", argv0);
}

}