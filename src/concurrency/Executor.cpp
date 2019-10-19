#include <afina/concurrency/Executor.h>

namespace Afina {
namespace Concurrency {

void perform(Executor *executor) {
    std::function<void()> task;
    bool perform_task = false;

    while (true) {
        {
            std::unique_lock<std::mutex> lock(executor->mutex);
            if (executor->empty_condition.wait_for(lock, executor->idle_time, [executor] {
                    return !(executor->tasks.empty()) || executor->state != Executor::State::kRun;
                })) {
                if (!executor->tasks.empty()) {
                    task = executor->tasks.front();
                    executor->tasks.pop_front();
                    perform_task = true;
                }
                if (executor->state != Executor::State::kRun) {
                    break;
                }
            } else if (executor->threads.size() <= executor->hight_watermark) {
                continue;
            }
        }
        if (perform_task) {
            task();
            perform_task = false;
        }
    }
    if (perform_task) {
        task();
    }
};

Executor::Executor(int l_w, int h_w, int m_q_s, std::chrono::milliseconds i_t)
    : low_watermark(l_w), hight_watermark(h_w), max_queue_size(m_q_s), idle_time(std::chrono::milliseconds(i_t)),
      state(State::kRun), live_threads(0) {
    for (int i = 0; i < low_watermark; ++i) {
        threads.emplace_back(std::thread(perform, this));
        //        live_threads++;
    }
}

// почему при реализации Executor::Execute(...) здесь, код не компилится ?

void Executor::Stop(bool await) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        Executor::state = Executor::State::kStopping;
    }
    {
        std::unique_lock<std::mutex> lock(mutex);
        while (!tasks.empty()){
            empty_condition.wait(lock);
        }
    }
    if (await){
        for (auto &it: threads){
            if (it.joinable()){
                it.join();
            }
        }
    } else {
        for (auto &it: threads){
            if (it.joinable()){
                it.detach();
            }
        }
    }
    {
        std::lock_guard<std::mutex> lock(mutex);
        Executor::state = Executor::State::kStopped;
    }
}

} // namespace Concurrency
} // namespace Afina
