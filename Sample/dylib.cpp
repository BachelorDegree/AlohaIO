#include "dylib_export.h"
#include "CoreDeps/include/SatelliteClient.hpp"
#include "Proto/hello.grpc.pb.h"

#include "Handler/SayHello.hpp"

HelloService::AsyncService service;

const char * EXPORT_Description(void)
{
    return "Sample Business Library";
}

void EXPORT_DylibInit(const char *conf_file)
{
    // do nothing
}

grpc::Service * EXPORT_GetGrpcServiceInstance(void)
{
    return &service;
}

void EXPORT_OnWorkerThreadStart(grpc::ServerCompletionQueue *cq)
{
    // Bind handlers
    new SayHelloHandler(&service, cq);
}

void EXPORT_BindSatelliteInstance(SatelliteClient *i)
{
    SatelliteClient::SetInstance(i);
}
