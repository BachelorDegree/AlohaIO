#pragma once

#include <grpcpp/server_context.h>

namespace grpc
{
    class ServerContext;
    class ServerCompletionQueue;
}

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
    explicit AsyncRpcHandler(grpc::ServerCompletionQueue *cq):
        status(Status::CREATE), cq(cq) { }

    virtual void Proceed(void) = 0;
    void SetStatusCreate(void) { status = Status::CREATE; }
    void SetStatusProcess(void) { status = Status::PROCESS; }
    void SetStatusFinish(void) { status = Status::FINISH; }

protected:
    Status                          status;
    grpc::ServerCompletionQueue*    cq;
    grpc::ServerContext             ctx;
    // You should add (when inherit from this):
    // YourRpcRequestPB                                     request;
    // YourRpcResponsePB                                    response;
    // grpc::ServerAsyncResponseWriter<YourRpcResponsePB>   responder;

    // ...and, the "service instance" itself if needed:
    // YourService::AsyncService*                           service;
};