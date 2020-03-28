#include <grpc/grpc.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <iostream>
#include <vector>
#include <thread>
#include <ctime>
#include <grpcpp/security/credentials.h>
#include "CoreDeps/AlohaIO/ContextHelper.hpp"
#include "Proto/hello.grpc.pb.h"
#include "Proto/hello.pb.h"
void run(){
  auto pChannel = grpc::CreateChannel("localhost:8964", grpc::InsecureChannelCredentials());
  HelloService::Stub oStub{pChannel};
  grpc::ClientContext oContext;
  HelloRequest oReq;
  HelloResponse oResp;
  oReq.set_greeting("hhhh");
  oStub.SayHello(&oContext, oReq, &oResp);
  std::cout<<oResp.ShortDebugString()<< ClientContextHelper(oContext).GetReturnCode() <<std::endl;
}
int main(){
  time_t oBegin = time(nullptr);
  std::vector<std::thread>                                    threads(100);
  for(int i=0;i<100;i++){
    threads[i] = std::thread(run);
  }
  for(int i=0;i<100;i++){
    threads[i].join();
  }
  time_t oEnd = time(nullptr);
  std::cout<<"begin: "<< oBegin <<" end: "<< oEnd<<std::endl;
  return 0;
}