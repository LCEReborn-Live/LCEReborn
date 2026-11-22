#ifndef PLATFORM_SWITCH_LIVE_WORKER_HPP
#define PLATFORM_SWITCH_LIVE_WORKER_HPP

#include <atomic>
#include <switch.h>

class SwitchLiveWorker
{
public:
    bool Start(void (*work)())
    {
        m_work = work;
        m_done = false;
        if (R_FAILED(threadCreate(&m_thread, Run, this, nullptr, 128 * 1024, 0x3b, -2)))
            return false;
        if (R_FAILED(threadStart(&m_thread)))
        {
            threadClose(&m_thread);
            return false;
        }
        m_started = true;
        return true;
    }

    bool Finished() const { return m_done; }

    void Join()
    {
        if (!m_started)
            return;
        threadWaitForExit(&m_thread);
        threadClose(&m_thread);
        m_started = false;
    }

private:
    static void Run(void* context)
    {
        auto& worker = *static_cast<SwitchLiveWorker*>(context);
        worker.m_work();
        worker.m_done = true;
    }

    Thread m_thread = {};
    std::atomic<bool> m_done{false};
    bool m_started = false;
    void (*m_work)() = nullptr;
};

#endif
