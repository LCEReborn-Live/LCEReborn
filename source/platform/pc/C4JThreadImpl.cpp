#include "platform/pc/C4JThreadImpl.hpp"

#include <chrono>
#include <cstdio>
#include <thread>
#if LCE_PLATFORM_SWITCH
#include <list>
#include <mutex>
#endif

#include "platform/Runtime.hpp"
#include "platform/pc/PcCrashReport.hpp"

static std::atomic<uint32_t> s_nextThreadId(1);
static thread_local C4JThreadImpl* s_currentThread = nullptr;
static C4JThreadImpl s_mainThread("Main thread");

#if LCE_PLATFORM_SWITCH
namespace
{
struct SwitchWorker
{
    std::thread thread;
    std::atomic<bool> finished{false};
};

struct SwitchWorkers
{
    std::mutex mutex;
    std::list<SwitchWorker> threads;
};

SwitchWorkers& GetSwitchWorkers()
{
    // Some workers run until the process exits. Their handles are reclaimed by Horizon.
    static SwitchWorkers* const workers = new SwitchWorkers;
    return *workers;
}
}

void C4JThreadImpl::ReapFinishedThreads()
{
    SwitchWorkers& workers = GetSwitchWorkers();
    std::lock_guard<std::mutex> lock(workers.mutex);
    for (auto worker = workers.threads.begin(); worker != workers.threads.end();)
    {
        if (worker->finished)
        {
            worker->thread.join();
            worker = workers.threads.erase(worker);
        }
        else
        {
            ++worker;
        }
    }
}
#endif

void Sleep(unsigned int milliseconds)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

void C4JThread::Sleep(int milliseconds)
{
    C4JThreadImpl::Sleep(milliseconds);
}

uint32_t C4JThread::getCurrentThread()
{
    return C4JThreadImpl::getCurrentThread();
}

bool C4JThread::isMainThread()
{
    return C4JThreadImpl::isMainThread();
}

const char* C4JThread::getCurrentThreadName()
{
    return C4JThreadImpl::getCurrentThreadName();
}

uint32_t C4JThreadImpl::getCurrentThread()
{
    return s_currentThread != nullptr ? s_currentThread->m_id : 0;
}

bool C4JThreadImpl::isMainThread()
{
    return s_currentThread == &s_mainThread;
}

const char* C4JThreadImpl::getCurrentThreadName()
{
    return s_currentThread != nullptr ? s_currentThread->getName() : "";
}

void C4JThreadImpl::entryPoint(C4JThreadImpl* thread)
{
    s_currentThread = thread;
    PcCrashReport::NameCurrentThread(thread->m_name);
    int result = thread->m_entry(thread->m_argument);
    thread->m_exitCode = static_cast<uint32_t>(result);
    if (thread->m_deleteOnExit)
    {
        delete thread;
        return;
    }
    thread->m_running = false;
    thread->m_completionEvent->Set();
}

C4JThreadImpl::C4JThreadImpl(int (*entry)(void*), void* argument, const char* name, int)
    : m_argument(argument), m_entry(entry), m_processor(-1), m_running(false), m_started(false),
      m_exitCode(STILL_ACTIVE), m_id(s_nextThreadId++), m_deleteOnExit(false),
      m_completionEvent(new C4JEventImpl(C4JEvent::Mode1))
{
    std::snprintf(m_name, sizeof(m_name), "(4J) %s", name);
}

C4JThreadImpl::C4JThreadImpl(const char* name)
    : m_argument(nullptr), m_entry(nullptr), m_processor(1), m_running(true), m_started(true), m_exitCode(STILL_ACTIVE),
      m_id(s_nextThreadId++), m_deleteOnExit(false), m_completionEvent(nullptr)
{
    std::snprintf(m_name, sizeof(m_name), "(4J) %s", name);
    s_currentThread = this;
}

C4JThreadImpl::~C4JThreadImpl()
{
    delete m_completionEvent;
}

void C4JThreadImpl::Run()
{
#if LCE_PLATFORM_SWITCH
    // libnx supports joining pthreads but does not implement detaching them.
    ReapFinishedThreads();
    SwitchWorkers& workers = GetSwitchWorkers();
    std::lock_guard<std::mutex> lock(workers.mutex);
    const auto worker = workers.threads.emplace(workers.threads.end());
    m_running = true;
    m_started = true;
    worker->thread = std::thread([this, worker] {
        entryPoint(this);
        worker->finished = true;
    });
#else
    m_running = true;
    m_started = true;
    std::thread(entryPoint, this).detach();
#endif
}

bool C4JThreadImpl::isRunning()
{
    return m_running;
}

bool C4JThreadImpl::hasStarted()
{
    return m_started;
}

void C4JThreadImpl::SetProcessor(int processor)
{
    m_processor = processor;
}

uint32_t C4JThreadImpl::GetProcessor()
{
    return static_cast<uint32_t>(m_processor);
}

void C4JThreadImpl::SetPriority(int)
{
}

uint32_t C4JThreadImpl::WaitForCompletion(int milliseconds)
{
    if (m_completionEvent != nullptr)
        return m_completionEvent->WaitForSignal(milliseconds);
    return 0;
}

int C4JThreadImpl::GetExitCode()
{
    return static_cast<int>(m_exitCode);
}

const char* C4JThreadImpl::getName()
{
    return m_name;
}

void C4JThreadImpl::SetDeleteOnExit(bool deleteOnExit)
{
    m_deleteOnExit = deleteOnExit;
}

void C4JThreadImpl::SetMainThreadID()
{
    s_currentThread = &s_mainThread;
    PcCrashReport::NameCurrentThread(s_mainThread.getName());
}

void SetSpecialThreadIDForFS_MilesStreamThread()
{
}
