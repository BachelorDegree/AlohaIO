#pragma once

namespace grpc
{
    class Service;
    class ServerCompletionQueue;
}

extern "C" 
{

const char *    EXPORT_Description(void);
void            EXPORT_DylibInit(const char *);
grpc::Service * EXPORT_GetGrpcServiceInstance(void);
void            EXPORT_OnWorkerThreadStart(grpc::ServerCompletionQueue*);
void            EXPORT_OnCoroutineWorkerStart(void);

}
