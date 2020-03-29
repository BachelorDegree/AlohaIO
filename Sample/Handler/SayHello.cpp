#include <iostream>
#include "SayHelloServiceImpl.hpp"
#include "SayHello.hpp"
#include "poll.h"

void SayHelloHandler::SetInterfaceName(void)
{
    interfaceName = "HelloService.SayHello";
}

void SayHelloHandler::Proceed(void)
{
    switch (status)
    {
    case Status::CREATE:
        this->SetStatusProcess();
        service->RequestSayHello(&ctx, &request, &responder, cq, cq, this);
        break;
    case Status::PROCESS:
    {
        // Firstly, spawn a new handler for next incoming RPC call
        new SayHelloHandler(service, cq);
        this->BeforeProcess();
        // Implement your logic here
        int iRet = SayHelloServiceImpl::GetInstance()->SayHello(request, response);
        this->SetReturnCode(iRet);
        this->SetStatusFinish();
        responder.Finish(response, grpc::Status::OK, this);
        break;
    }
    case Status::FINISH:
        delete this;
        break;
    default:
        // throw exception
        ;
    }
}
