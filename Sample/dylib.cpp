#include "dylib_export.h"
#include "Proto/hello.grpc.pb.h"
#include "SayHelloServiceImpl.hpp"
#include "Handler/SayHello.hpp"

HelloService::AsyncService service;

const char * EXPORT_Description(void)
{
    return "Sample Business Library";
}

void EXPORT_DylibInit(const char *conf_file)
{
    SayHelloServiceImpl::BeforeServerStart(conf_file);
}

grpc::Service * EXPORT_GetGrpcServiceInstance(void)
{
    return &service;
}

void EXPORT_OnWorkerThreadStart(grpc::ServerCompletionQueue *cq)
{
    SayHelloServiceImpl::SetInstance(new SayHelloServiceImpl);
    SayHelloServiceImpl::GetInstance()->BeforeWorkerStart();
    // Bind handlers
    new SayHelloHandler(&service, cq);
}

void EXPORT_OnCoroutineWorkerStart(void)
{

}