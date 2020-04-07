#pragma once

namespace AlohaIO
{
class TfcConfigCodec;
}

void SetSpdlog(AlohaIO::TfcConfigCodec&);
void SetLogger(AlohaIO::TfcConfigCodec&);
void SetLogLevel(const char *);