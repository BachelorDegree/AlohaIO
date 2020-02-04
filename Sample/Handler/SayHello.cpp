#include "SayHello.hpp"

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
        // Implement your logic here
        response.set_reply(request.greeting());
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
