#pragma once
#include <poll.h>
#include "Proto/hello.pb.h"
class SayHelloServiceImpl{
  public:
    static SayHelloServiceImpl *GetInstance();
    static void SetInstance(SayHelloServiceImpl *);
    static int BeforeServerStart(const char * czConf) {
      return 0;
    }
    int BeforeWorkerStart() {
      return 0;
    }
    int SayHello(const HelloRequest & oReq, HelloResponse & oResp){
      int iCode = atoi(oReq.greeting().c_str());
      oResp.set_reply(oReq.greeting());
      poll(0, 0, iCode % 1000);
      return iCode;
    }
};