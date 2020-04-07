#include <map>
#include <string>
#include <stdexcept>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include "SetSpdlog.hpp"
#include "coredeps/TfcConfigCodec.hpp"

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

void SetSpdlog(AlohaIO::TfcConfigCodec &oConf)
{
    SetLogger(oConf);
    
    if (oConf.HasKV("log", "level"))
    {
        SetLogLevel(oConf.GetKV("server", "level").c_str());
    }

    if (oConf.HasKV("log", "pattern"))
    {
        spdlog::set_pattern(oConf.GetKV("log", "pattern"));
    }
}

void SetLogLevel(const char *apLevel)
{
    auto itPair = mapStrToEnum.find(apLevel);
    if (itPair == mapStrToEnum.end())
        return;
    spdlog::set_level(itPair->second);
}

void SetLogger(AlohaIO::TfcConfigCodec &oConf)
{
    if (!oConf.HasKV("log", "type"))
        return;
    const auto sType = oConf.GetKV("log", "type");
    const auto sPath = oConf.GetKV("log", "path");
    if (sType == "stdout")
        return;
    if (sPath.empty())
    {
        throw std::invalid_argument("Log path is empty");
    }

    std::shared_ptr<spdlog::logger> oLogger;
    if (sType == "file")
    {
        oLogger = spdlog::basic_logger_mt("basic_logger", sPath);
    }
    else if (sType == "rotating")
    {
        const auto sMaxSize = oConf.GetKV("log\\rotating", "max_size");
        const auto sMaxFile = oConf.GetKV("log\\rotating", "max_file");
        
        char *pPtr;
        auto ulMaxSize = strtoul(sMaxSize.c_str(), &pPtr, 10);
        auto ulMaxFile = strtoul(sMaxFile.c_str(), &pPtr, 10);

        if (ulMaxSize == 0 || ulMaxFile == 0)
        {
            throw std::invalid_argument("Rotating log argument not correct");
        }

        oLogger = spdlog::rotating_logger_mt("rotating_logger", sPath, ulMaxSize, ulMaxFile);
    }
    else if (sType == "daily")
    {
        const auto sHour = oConf.GetKV("log\\daily", "refresh_at_hour");
        const auto sMinute = oConf.GetKV("log\\daily", "refresh_at_minute");
        
        char *pPtr;
        auto iHour = static_cast<int>(strtol(sHour.c_str(), &pPtr, 10));
        auto iMinute = static_cast<int>(strtol(sMinute.c_str(), &pPtr, 10));
        oLogger = spdlog::daily_logger_mt("daily_logger", sPath, iHour, iMinute);
    }
    else
    {
        return;
    }
    spdlog::set_default_logger(oLogger);
}
