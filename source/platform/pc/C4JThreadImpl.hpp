#ifndef PLATFORM_PC_C4JTHREADIMPL_HPP
#define PLATFORM_PC_C4JTHREADIMPL_HPP

#include <atomic>
#include <stdint.h>

#include "platform/C4JThread.hpp"
#include "platform/pc/C4JEventImpl.hpp"

void Sleep(unsigned int milliseconds);

class C4JThreadImpl : public C4JThread
{
public:
    C4JThreadImpl(int (*entry)(void*), void* argument, const char* name, int stackSize);
    C4JThreadImpl(const char* name);
    virtual ~C4JThreadImpl();

    static uint32_t getCurrentThread();
    static bool isMainThread();
    static const char* getCurrentThreadName();
    static void Sleep(int milliseconds) { ::Sleep(milliseconds); }

    virtual void Run();
    virtual bool isRunning();
    virtual bool hasStarted();
    virtual void SetProcessor(int processor);
    virtual uint32_t GetProcessor();
    virtual void SetPriority(int priority);
    virtual uint32_t WaitForCompletion(int milliseconds);
    virtual int GetExitCode();
    virtual const char* getName();
    virtual void SetDeleteOnExit(bool deleteOnExit);
    static void SetMainThreadID();
#if LCE_PLATFORM_SWITCH
    static void ReapFinishedThreads();
#endif

private:
    static void entryPoint(C4JThreadImpl* thread);

    void* m_argument;
    int (*m_entry)(void*);
    char m_name[64];
    int m_processor;
    std::atomic<bool> m_running;
    std::atomic<bool> m_started;
    std::atomic<uint32_t> m_exitCode;
    uint32_t m_id;
    bool m_deleteOnExit;
    C4JEventImpl* m_completionEvent;
};

#endif
