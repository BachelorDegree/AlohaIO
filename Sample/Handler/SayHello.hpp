#pragma once

#include "../AsyncRpcHandler.hpp"
#include "../Proto/hello.grpc.pb.h"

class SayHelloHandler final : public AsyncRpcHandler
{
public:
    SayHelloHandler(HelloService::AsyncService *service, grpc::ServerCompletionQueue *cq):
        AsyncRpcHandler(cq), service(service), responder(&ctx)
    {
        this->Proceed();
    }
    void Proceed(void) override;

private:
    HelloService::AsyncService*                     service;
    HelloRequest                                    request;
    HelloResponse                                   response;
    grpc::ServerAsyncResponseWriter<HelloResponse>  responder;
};