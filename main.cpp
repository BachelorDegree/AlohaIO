#include <thread>
#include <memory>
#include <csignal>
#include <cstdlib>
#include <unistd.h>

#include <grpc/grpc.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/security/server_credentials.h>

#include "Util/DaemonUtil.hpp"
#include "Util/DylibManager.hpp"
#include "Sample/dylib_export.h"
#include "Sample/AsyncRpcHandler.hpp"
#include "Common/TfcConfigCodec.hpp"

AlohaIO::TfcConfigCodec MainConf;

void DoServer(void);

int main(int argc, char *argv[])
{
    int MainRet = EXIT_FAILURE;
    do
    {
        if (argc == 1)
        {
            AlohaIO::PrintUsage(argv[0]);
            break;
        }
        
        int ret = MainConf.ParseFile(argv[1]);
        if (ret != AlohaIO::TfcConfigCodec::SUCCESS)
        {
            fprintf(stderr, "Parse %s failed, retcode: %d\n", argv[1], ret);
            break;
        }

        if (!MainConf.HasSection("libs"))
        {
            fprintf(stderr, "%s doesn't have section `libs`\n", argv[1]);
            break;
        }

        for (const auto &i : MainConf.GetSection("libs").Pairs)
        {
            AlohaIO::DylibManager::GetInstance().LoadLibrary(i.Value, i.Key);
            auto desc = AlohaIO::DylibManager::GetInstance().GetSymbol<decltype(&EXPORT_Description)>(i.Key, "EXPORT_Description");
            if (desc == nullptr)
            {
                fprintf(stderr, "Cannot find symbol `EXPORT_Description` in %s\n", i.Value.c_str());
                break;
            }
            fprintf(stdout, "Dylib %s (path: %s) loaded, description: %s\n", i.Key.c_str(), i.Value.c_str(), desc());
        }

        if (argc > 2) // not daemon
        {
            fprintf(stdout, "%s: foreground mode\n", argv[0]);
            DoServer();
        }
        else // daemon
        {
            fprintf(stdout, "%s: daemon mode\n", argv[0]);
            auto pid = fork();
            if (pid == -1)
            {
                fprintf(stderr, "%s", "fork() failed!");
                break;
            }
            if (pid != 0) // parent
                break;
            if (setsid() == -1)
            {
                fprintf(stderr, "%s", "setsid() failed!");
                break;
            }
            DoServer();
        }
        MainRet = EXIT_SUCCESS;
    } while (false);
    return MainRet;
}

void DoServer(void)
{
    grpc::ServerBuilder builder;

    auto listenIpPort = MainConf.GetKV("server", "bind_ip").append(":").append(MainConf.GetKV("server", "bind_port"));
    builder.AddListeningPort(listenIpPort, grpc::InsecureServerCredentials());
    for (const auto &i : MainConf.GetSection("libs").Pairs)
    {
        auto init = AlohaIO::DylibManager::GetInstance().GetSymbol<decltype(&EXPORT_DylibInit)>(i.Key, "EXPORT_DylibInit");
        auto getService = AlohaIO::DylibManager::GetInstance().GetSymbol<decltype(&EXPORT_GetGrpcServiceInstance)>(i.Key, "EXPORT_GetGrpcServiceInstance");
        init();
        builder.RegisterService(getService());
    }
    
    // Start threads and register handlers (from dylib)
    auto threadNum = atoi(MainConf.GetKV("server", "worker_thread_num").c_str());
    std::vector<std::thread>                                    threads(threadNum);
    std::vector<std::unique_ptr<grpc::ServerCompletionQueue>>   completionQueues;
    for (int _i = 0; _i < threadNum; ++_i)
        completionQueues.push_back(builder.AddCompletionQueue());

    auto server = builder.BuildAndStart();

    for (int _i = 0; _i < threadNum; ++_i)
    {
        threads[_i] = std::thread([&, _i]()
        {
            
            // Bind RPC handlers
            for (const auto &i : MainConf.GetSection("libs").Pairs)
            {
                auto workerFunc = AlohaIO::DylibManager::GetInstance().GetSymbol<decltype(&EXPORT_OnWorkerThreadStart)>(i.Key, "EXPORT_OnWorkerThreadStart");
                workerFunc(completionQueues[_i].get());
            }
            // Loop
            for ( ; ;)
            {
                bool ok;
                void *tag;
                if (completionQueues[_i]->Next(&tag, &ok) == false)
                    break;
                GPR_ASSERT(ok == true);
                GPR_ASSERT(tag != nullptr);
                reinterpret_cast<AsyncRpcHandler*>(tag)->Proceed();
            }
        });
    }
    for (auto &&t : threads)
        t.join();

    server->Shutdown();
    for (auto &i : completionQueues)
    {
        bool ok;
        void *tag;
        while (i->Next(&tag, &ok));
        i->Shutdown();
    }
}