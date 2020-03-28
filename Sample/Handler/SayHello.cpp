#include "SayHello.hpp"
#include "poll.h"
#include <iostream>
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
        // Firstly, spawn a new handler for next incoming RPC call
        new SayHelloHandler(service, cq);
        this->BeforeProcess();
        // Implement your logic here
        std::cout<<"wait start"<<std::endl;
        poll(0, 0, 1000);
        std::cout<<"wait end"<<std::endl;
        response.set_reply(request.greeting());
        this->SetReturnCode(100);
        this->SetStatusFinish();
        responder.Finish(response, grpc::Status::OK, this);
        break;
    case Status::FINISH:
        delete this;
        break;
    default:
        // throw exception
        ;
    }
}
