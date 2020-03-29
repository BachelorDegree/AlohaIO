#include <colib/co_routine_specific.h>
#include "SayHelloServiceImpl.hpp"
struct __SayHelloServiceImplWrapper{
  SayHelloServiceImpl * pImpl;
};
CO_ROUTINE_SPECIFIC(__SayHelloServiceImplWrapper, g_coSayHelloServiceImplWrapper)
SayHelloServiceImpl *SayHelloServiceImpl::GetInstance()
{
  return g_coSayHelloServiceImplWrapper->pImpl;
}
void SayHelloServiceImpl::SetInstance(SayHelloServiceImpl *pImpl)
{
  g_coSayHelloServiceImplWrapper->pImpl = pImpl;
}