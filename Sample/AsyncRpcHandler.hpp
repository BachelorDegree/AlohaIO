#pragma once
#include <iostream>
#include <grpcpp/server_context.h>
#include "coredeps/ContextHelper.hpp"
namespace grpc
{
class ServerContext;
class ServerCompletionQueue;
} // namespace grpc

class AsyncRpcHandler
{
public:
    enum class Status
    {
        CREATE,
        PROCESS,
        FINISH
    };
    AsyncRpcHandler(void) = delete;
    explicit AsyncRpcHandler(grpc::ServerCompletionQueue *cq) : status(Status::CREATE), cq(cq) {}

    virtual void Proceed(void) = 0;

    virtual void SetInterfaceName(void) = 0;
    const std::string &GetInterfaceName(void) const { return interfaceName; }

    void SetStatusCreate(void) { status = Status::CREATE; }
    void SetStatusProcess(void) { status = Status::PROCESS; }
    void SetStatusFinish(void) { status = Status::FINISH; }
    void BeforeProcess(void) { ServerContextHelper::GetInstance()->BindContext(ctx); }
    void SetReturnCode(int iRet) { ServerContextHelper::GetInstance()->SetReturnCode(iRet); }

protected:
    Status status;
    grpc::ServerCompletionQueue *cq;
    grpc::ServerContext ctx;
    std::string interfaceName;
    // You should add (when inherit from this):
    // YourRpcRequestPB                                     request;
    // YourRpcResponsePB                                    response;
    // grpc::ServerAsyncResponseWriter<YourRpcResponsePB>   responder;

    // ...and, the "service instance" itself if needed:
    // YourService::AsyncService*                           service;
};