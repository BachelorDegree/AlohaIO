#include <thread>
#include <memory>
#include <csignal>
#include <cstdlib>
#include <unistd.h>
#include <stack>
#include <grpc/grpc.h>
#include <grpc/impl/codegen/gpr_types.h>
#include <spdlog/spdlog.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/security/server_credentials.h>

#include "Util/DaemonUtil.hpp"
#include "Util/DylibManager.hpp"
#include "Sample/dylib_export.h"
#include "Sample/AsyncRpcHandler.hpp"

#include <colib/co_aio.h>
#include <colib/co_routine.h>
#include <colib/co_routine_inner.h>
#include <colib/co_routine_specific.h>
#include "coredeps/ContextHelper.hpp"
#include "coredeps/TfcConfigCodec.hpp"
#include "coredeps/SatelliteClient.hpp"

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
template <class T>
int co_create(stCoRoutine_t **co, const stCoRoutineAttr_t *attr, T func)
{
    T* _func = new T(func);
    return co_create(co, attr, [](void *arg) -> void * {
        T & _func = *reinterpret_cast<T *>(arg);
        _func();
        delete &_func;
        return 0;
    },
                     (void *)_func);
}
void *co_worker_func(void *arg)
{
    co_enable_hook_sys();
    co_worker_t *pWorker = reinterpret_cast<co_worker_t *>(arg);
    ServerContextHelper::SetInstance(new ServerContextHelper);
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
    int iMainRet = EXIT_FAILURE;
    do
    {
        if (argc == 1)
        {
            AlohaIO::PrintUsage(argv[0]);
            break;
        }

        int iRet = MainConf.ParseFile(argv[1]);
        if (iRet != AlohaIO::TfcConfigCodec::SUCCESS)
        {
            spdlog::error("Parse {} failed, retcode: {}", argv[1], iRet);
            break;
        }

        if (!MainConf.HasSection("libs"))
        {
            spdlog::error("{} doesn't have section `libs`", argv[1]);
            break;
        }

        if (argc > 2) // not daemon
        {
            spdlog::info("{}: foreground mode", argv[0]);
            DoServer();
        }
        else // daemon
        {
            spdlog::info("{}: daemon mode", argv[0]);
            auto iPid = fork();
            if (iPid == -1)
            {
                spdlog::error("fork() failed!");
                break;
            }
            if (iPid != 0) // parent
                break;
            if (setsid() == -1)
            {
                spdlog::error("setsid() failed!");
                break;
            }
            DoServer();
        }
        iMainRet = EXIT_SUCCESS;
    } while (false);
    return iMainRet;
}

void DoServer(void)
{
    // Name service
    if (MainConf.HasSection("satellite"))
    {
        spdlog::info("Satellite server count: {}", MainConf.GetSection("satellite\\servers").Pairs.size());
        for (auto &i : MainConf.GetSection("satellite\\servers").Pairs)
        {
            SatelliteClient::GetInstance().SetServer(i.Value);
        }
    }

    grpc::ServerBuilder oServerBuilder;

    auto sListenIpPort = MainConf.GetKV("server", "bind_ip").append(":").append(MainConf.GetKV("server", "bind_port"));
    oServerBuilder.AddListeningPort(sListenIpPort, grpc::InsecureServerCredentials());
    for (const auto &oBizLib : MainConf.GetSection("libs").Children)
    {
        auto sLibraryName = oBizLib.Tag;
        auto sDylibPath = MainConf.GetKV(string("libs\\").append(sLibraryName).c_str(), "dylib_path");
        AlohaIO::DylibManager::GetInstance().LoadLibrary(sDylibPath, sLibraryName);
        auto pfDescription = AlohaIO::DylibManager::GetInstance().GetSymbol<decltype(&EXPORT_Description)>(sLibraryName, "EXPORT_Description");
        if (pfDescription == nullptr)
        {
            spdlog::error("Cannot find symbol `EXPORT_Description` in {}", sDylibPath);
            break;
        }
        spdlog::info("Dylib {} (path: {}) loaded, description: {}", sLibraryName, sDylibPath, pfDescription());
        auto pfDylibInit = AlohaIO::DylibManager::GetInstance().GetSymbol<decltype(&EXPORT_DylibInit)>(sLibraryName, "EXPORT_DylibInit");
        auto pfGetServiceInstance = AlohaIO::DylibManager::GetInstance().GetSymbol<decltype(&EXPORT_GetGrpcServiceInstance)>(sLibraryName, "EXPORT_GetGrpcServiceInstance");
        auto sConfigFile = MainConf.GetKV(string("libs\\").append(sLibraryName).c_str(), "config_file");
        pfDylibInit(sConfigFile.c_str());
        oServerBuilder.RegisterService(pfGetServiceInstance());

        auto sCanonicalServiceName = MainConf.GetKV(string("libs\\").append(sLibraryName).c_str(), "canonical_service_name");
        auto sNetworkInterface = MainConf.GetKV("satellite", "bind_interface");
        SatelliteClient::GetInstance().RegisterLocalService(sCanonicalServiceName, sNetworkInterface, MainConf.GetKV("server", "bind_port"));
    }

    // Start threads and register handlers (from dylib)
    auto iThreadNum = atoi(MainConf.GetKV("server", "worker_thread_num").c_str());
    auto iCoNum = atoi(MainConf.GetKV("server", "worker_co_num").c_str());
    std::vector<std::thread> vecThreads(iThreadNum);
    std::vector<std::shared_ptr<grpc::ServerCompletionQueue>> vecCompletionQueues;
    for (int _i = 0; _i < iThreadNum; ++_i)
        vecCompletionQueues.push_back(oServerBuilder.AddCompletionQueue());

    auto pServer = oServerBuilder.BuildAndStart();

    for (int _i = 0; _i < iThreadNum; ++_i)
    {
        vecThreads[_i] = std::thread([&, _i]() {
            // Bind RPC handlers
            if (iCoNum <= 0)
            {
                for (const auto &i : MainConf.GetSection("libs").Children)
                {
                    auto pfOnWorkerStart = AlohaIO::DylibManager::GetInstance().GetSymbol<decltype(&EXPORT_OnWorkerThreadStart)>(i.Tag, "EXPORT_OnWorkerThreadStart");
                    pfOnWorkerStart(vecCompletionQueues[_i].get());
                }
                ServerContextHelper::SetInstance(new ServerContextHelper);
                // No coroutine mode
                for (;;)
                {
                    bool bOk;
                    void *pTag;
                    if (vecCompletionQueues[_i]->Next(&pTag, &bOk) == false)
                        break;
                    GPR_ASSERT(bOk == true);
                    GPR_ASSERT(pTag != nullptr);
                    reinterpret_cast<AsyncRpcHandler *>(pTag)->Proceed();
                }
            }
            else
            {
                CoRoutineSetSpecificCallback([](pthread_key_t key) -> void * { return co_getspecific(key); }, [](pthread_key_t key, const void *value) -> int { return co_setspecific(key, value); });
                // Initialize co workers
                co_control_t oControl;
                oControl.pCq = vecCompletionQueues[_i];
                oControl.pFreeWorkerStack = std::make_shared<std::stack<co_worker_t *>>();
                std::vector<co_worker_t> vecWorkers;
                vecWorkers.resize(iCoNum);
                co_aio_init_ct(); // Initialize colib AIO
                for (int j = 0; j < iCoNum; j++)
                {
                    vecWorkers[j].pHandler = nullptr;
                    vecWorkers[j].pStack = oControl.pFreeWorkerStack;
                    oControl.pFreeWorkerStack->push(&vecWorkers[j]);
                    
                    co_create(&vecWorkers[j].co, 0, [&, j, _i]() -> void {
                        co_worker_t &oWorker = vecWorkers[j];
                        for (const auto &i : MainConf.GetSection("libs").Children)
                        {
                            auto pfOnWorkerStart = AlohaIO::DylibManager::GetInstance().GetSymbol<decltype(&EXPORT_OnWorkerThreadStart)>(i.Tag, "EXPORT_OnWorkerThreadStart");
                            pfOnWorkerStart(vecCompletionQueues[_i].get());
                        }
                        co_enable_hook_sys();
                        ServerContextHelper::SetInstance(new ServerContextHelper);
                        while (true)
                        {
                            if (oWorker.pHandler == nullptr)
                            {
                                //no work, sleep
                                co_yield_ct();
                                continue;
                            }
                            oWorker.pHandler->Proceed();
                            oWorker.pHandler = nullptr;
                            oWorker.pStack->push(&oWorker);
                        }
                    });
                    co_resume(vecWorkers[j].co);
                }
                // Start event loop to accept (without hook sys function)
                // TODO: may be use sys hook to have better performance?
                stCoEpoll_t *ev = co_get_epoll_ct();
                co_eventloop(ev, [](void *arg) -> int {
                    co_control_t *pControl = reinterpret_cast<co_control_t *>(arg);
                    while (true)
                    {
                        if (pControl->pFreeWorkerStack->empty())
                        {
                            return 0;
                        }
                        bool ok;
                        void *pTag;

                        switch (pControl->pCq->AsyncNext(&pTag, &ok, gpr_timespec{0, 1, GPR_TIMESPAN}))
                        {
                        case grpc::CompletionQueue::NextStatus::GOT_EVENT:
                        {
                            GPR_ASSERT(ok == true);
                            GPR_ASSERT(pTag != nullptr);
                            auto ev = co_get_epoll_ct();
                            auto pWorker = pControl->pFreeWorkerStack->top();
                            pControl->pFreeWorkerStack->pop();
                            GPR_ASSERT(pWorker->pHandler == nullptr);
                            pWorker->pHandler = reinterpret_cast<AsyncRpcHandler *>(pTag);
                            co_resume(pWorker->co);
                            break;
                        }
                        case grpc::CompletionQueue::NextStatus::SHUTDOWN:
                            return -1;
                        case grpc::CompletionQueue::NextStatus::TIMEOUT:
                            return 0;
                        }
                    }

                    return 0;
                },
                             (void *)&oControl);
            }
        });
    }
    for (auto &&t : vecThreads)
        t.join();

    pServer->Shutdown();
    for (auto &i : vecCompletionQueues)
    {
        bool ok;
        void *pTag;
        while (i->Next(&pTag, &ok))
            ;
        i->Shutdown();
    }
}
