#include <afina/concurrency/Executor.h>

namespace Afina {
namespace Concurrency {

void perform(Executor *executor) {
    std::function<void()> task;
    bool last_task = false;

    for (;;) {
        {
            std::unique_lock<std::mutex> lock(executor->mutex);
            if (executor->empty_condition.wait_for(lock, executor->idle_time, [executor] {
                    return !(executor->tasks.empty()) || executor->state != Executor::State::kRun;
                })) {
                if (!executor->tasks.empty()) {
                    task = executor->tasks.front();
                    executor->tasks.pop_front();
                    executor->threads[std::this_thread::get_id()] = 1;
                    executor->busy_threads_count++;
                }
                if (executor->state != Executor::State::kRun) {
                    if (executor->threads[std::this_thread::get_id()] == 1){
                        last_task = true;
                    } else {
                        return;
                    }
                }
            } else if (executor->threads.size() <= executor->low_watermark) {
                continue;
            }
        }

        task();
        {
            std::lock_guard<std::mutex> lg(executor->mutex);
            executor->threads[std::this_thread::get_id()] = 0;
            executor->busy_threads_count--;
        }

        if (last_task){
            std::lock_guard<std::mutex> lg(executor->mutex);
            auto it = executor->threads.find(std::this_thread::get_id());
            if (it != executor->threads.end()){
                executor->threads.erase(it);
            }
            return;
        }
    }
};

Executor::Executor(int l_w, int h_w, int m_q_s, std::chrono::milliseconds i_t)
    : low_watermark(l_w), hight_watermark(h_w), max_queue_size(m_q_s), idle_time(std::chrono::milliseconds(i_t)),
      state(State::kRun) {
    for (int i = 0; i < low_watermark; ++i) {
        auto new_thread = std::thread(perform, this);
        threads.insert(std::make_pair(new_thread.get_id(), 0));
        new_thread.detach();
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
        while (!tasks.empty()) {
            empty_condition.wait(lock);
        }
    }
//    if (await) {
//        for (auto &it : threads) {
//            if (it.joinable()) {
//                it.join();
//            }
//        }
//    } else {
//        for (auto &it : threads) {
//            if (it.joinable()) {
//                it.detach();
//            }
//        }
//    }
    {
        std::lock_guard<std::mutex> lock(mutex);
        Executor::state = Executor::State::kStopped;
    }
}

} // namespace Concurrency
} // namespace Afina
