#include <thread>
#include <memory>
#include <csignal>
#include <cstdlib>
#include <unistd.h>
#include <stack>
#include <grpc/impl/codegen/gpr_types.h>
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

#include "CoreDeps/libco/co_closure.h"
#include "CoreDeps/libco/co_routine.h"

#include "CoreDeps/include/SatelliteClient.hpp"
AlohaIO::TfcConfigCodec MainConf;

void DoServer(void);
struct co_worker_t
{
    stCoRoutine_t *co;
    AsyncRpcHandler *pHandler;
    std::shared_ptr<std::stack<co_worker_t *>> pStack;
};
struct co_control_t
{
    std::shared_ptr<std::stack<co_worker_t *>> pFreeWorkerStack;
    std::shared_ptr<grpc::ServerCompletionQueue> pCq;
};
void *co_worker_func(void *arg)
{
    co_enable_hook_sys();
    co_worker_t *pWorker = reinterpret_cast<co_worker_t *>(arg);
    while (true)
    {
        if (pWorker->pHandler == nullptr)
        {
            //no work, sleep
            co_yield_ct();
            continue;
        }
        pWorker->pHandler->Proceed();
        pWorker->pHandler = nullptr;
        pWorker->pStack->push(pWorker);
    }
}

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
    // Name service
    if (MainConf.HasSection("satellite"))
    {
        fprintf(stdout, "Satellite server count: %lu\n", MainConf.GetSection("satellite\\servers").Pairs.size());
        for (auto &i : MainConf.GetSection("satellite\\servers").Pairs)
        {
            SatelliteClient::GetInstance().SetServer(i.Value);
        }
    }

    grpc::ServerBuilder builder;

    auto listenIpPort = MainConf.GetKV("server", "bind_ip").append(":").append(MainConf.GetKV("server", "bind_port"));
    builder.AddListeningPort(listenIpPort, grpc::InsecureServerCredentials());
    for (const auto &bizlib : MainConf.GetSection("libs").Children)
    {
        auto libname = bizlib.Tag;
        auto init = AlohaIO::DylibManager::GetInstance().GetSymbol<decltype(&EXPORT_DylibInit)>(libname, "EXPORT_DylibInit");
        auto getService = AlohaIO::DylibManager::GetInstance().GetSymbol<decltype(&EXPORT_GetGrpcServiceInstance)>(libname, "EXPORT_GetGrpcServiceInstance");
        auto bindSatellite = AlohaIO::DylibManager::GetInstance().GetSymbol<decltype(&EXPORT_BindSatelliteInstance)>(libname, "EXPORT_BindSatelliteInstance");
        auto conf_file = MainConf.GetKV(string("libs\\").append(libname).c_str(), "config_file");
        init(conf_file.c_str());
        builder.RegisterService(getService());
        bindSatellite(&SatelliteClient::GetInstance());

        auto service_name = MainConf.GetKV(string("libs\\").append(libname).c_str(), "canonical_service_name");
        auto network_interface = MainConf.GetKV("satellite", "bind_interface");
        SatelliteClient::GetInstance().RegisterLocalService(service_name, network_interface, MainConf.GetKV("server", "bind_port"));
    }

    // Start threads and register handlers (from dylib)
    auto threadNum = atoi(MainConf.GetKV("server", "worker_thread_num").c_str());
    auto coNum = atoi(MainConf.GetKV("server", "worker_co_num").c_str());
    std::vector<std::thread> threads(threadNum);
    std::vector<std::shared_ptr<grpc::ServerCompletionQueue>> completionQueues;
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
            // Initial co workers

            co_control_t oControl;
            oControl.pCq = completionQueues[_i];
            oControl.pFreeWorkerStack = std::make_shared<std::stack<co_worker_t *>>();
            std::vector<co_worker_t> vecWorkers;
            vecWorkers.resize(coNum);
            for (int j = 0; j < coNum; j++)
            {
                vecWorkers[j].pHandler = nullptr;
                vecWorkers[j].pStack = oControl.pFreeWorkerStack;
                oControl.pFreeWorkerStack->push(&vecWorkers[j]);
                co_create(&vecWorkers[j].co, 0, co_worker_func, (void *)&vecWorkers[j]);
                co_resume(vecWorkers[j].co);
            }
            // Start event loop to accept (without hook sys function)
            // TODO: may be use sys hook to have better performance?
            stCoEpoll_t *ev = co_get_epoll_ct();
            co_eventloop(ev, [](void *arg) -> int {
                co_control_t *pControl = reinterpret_cast<co_control_t *>(arg);
                if (pControl->pFreeWorkerStack->empty())
                {
                    return 0;
                }
                bool ok;
                void *tag;

                switch (pControl->pCq->AsyncNext(&tag, &ok, gpr_timespec{0, 100, GPR_TIMESPAN}))
                {
                case grpc::CompletionQueue::NextStatus::GOT_EVENT:
                {

                    GPR_ASSERT(ok == true);
                    GPR_ASSERT(tag != nullptr);
                    stCoEpoll_t *ev = co_get_epoll_ct();
                    auto pWorker = pControl->pFreeWorkerStack->top();
                    pControl->pFreeWorkerStack->pop();
                    GPR_ASSERT(pWorker->pHandler == nullptr);
                    pWorker->pHandler = reinterpret_cast<AsyncRpcHandler *>(tag);
                    co_resume(pWorker->co);
                }
                break;
                case grpc::CompletionQueue::NextStatus::SHUTDOWN:
                    return -1;
                case grpc::CompletionQueue::NextStatus::TIMEOUT:
                    break;
                }
                return 0;
            },
                         (void *)&oControl);
        });
    }
    for (auto &&t : threads)
        t.join();

    server->Shutdown();
    for (auto &i : completionQueues)
    {
        bool ok;
        void *tag;
        while (i->Next(&tag, &ok))
            ;
        i->Shutdown();
    }
}
