#include <map>
#include <string>
#include <spdlog/spdlog.h>
#include "SetLogLevel.hpp"

static std::map<std::string, spdlog::level::level_enum> mapStrToEnum
{
    { "trace", spdlog::level::trace },
    { "debug", spdlog::level::debug },
    { "info", spdlog::level::info },
    { "warn", spdlog::level::warn },
    { "error", spdlog::level::err },
    { "critical", spdlog::level::critical },
    { "off", spdlog::level::off }
};

void SetLogLevel(const char *apLevel)
{
    auto itPair = mapStrToEnum.find(apLevel);
    if (itPair == mapStrToEnum.end())
        return;
    spdlog::set_level(itPair->second);
}