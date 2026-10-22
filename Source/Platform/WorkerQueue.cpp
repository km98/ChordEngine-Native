#include "Platform.h"

#include <chrono>

namespace chordengine::platform
{
struct WorkerQueue::Shared
{
    std::mutex mutex;
    std::condition_variable wake;
    std::condition_variable idle;
    std::deque<std::function<void()>> jobs;
    bool stopping = false;
    bool running = false;
};

WorkerQueue::WorkerQueue(int shutdownWaitMs)
    : shared_(std::make_shared<Shared>()), shutdownWaitMs_(shutdownWaitMs)
{
    thread_ = std::thread([shared = shared_]
    {
        for (;;)
        {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(shared->mutex);
                shared->wake.wait(lock, [&] { return shared->stopping || !shared->jobs.empty(); });
                if (shared->stopping)
                    return;
                job = std::move(shared->jobs.front());
                shared->jobs.pop_front();
                shared->running = true;
            }
            job();
            {
                std::lock_guard<std::mutex> lock(shared->mutex);
                shared->running = false;
            }
            shared->idle.notify_all();
        }
    });
}

WorkerQueue::~WorkerQueue()
{
    bool finished = true;
    {
        std::unique_lock<std::mutex> lock(shared_->mutex);
        shared_->jobs.clear();
        finished = shared_->idle.wait_for(lock, std::chrono::milliseconds(shutdownWaitMs_),
                                          [&] { return !shared_->running; });
        shared_->stopping = true;
    }
    shared_->wake.notify_all();
    if (finished)
        thread_.join();
    else
        thread_.detach(); // the job holds only its own captures and `shared`
}

void WorkerQueue::post(std::function<void()> job)
{
    {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        if (shared_->stopping)
            return;
        shared_->jobs.push_back(std::move(job));
    }
    shared_->wake.notify_one();
}

void WorkerQueue::clearPending()
{
    std::lock_guard<std::mutex> lock(shared_->mutex);
    shared_->jobs.clear();
}
} // namespace chordengine::platform
