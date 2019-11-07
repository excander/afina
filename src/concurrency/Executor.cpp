#include <afina/concurrency/Executor.h>
#include <iostream>
#include <unistd.h>

namespace Afina {
namespace Concurrency {

void perform(Executor *executor) {
    std::function<void()> task;
    bool last_task = false;

    for (;;) {
        {
            std::unique_lock<std::mutex> lock(executor->mutex);
            //            std::cout << executor->busy_threads_count << " " << executor->threads.size() << std::endl;
            if (executor->tasks.empty()) {
                if (executor->empty_condition.wait_for(lock, std::chrono::milliseconds(100), [executor] {
                        return (!(executor->tasks.empty()) || (executor->state != Executor::State::kRun));
                    })) { // просыпание по условию
                    if (!executor->tasks.empty()) {
                        task = executor->tasks.front();
                        executor->tasks.pop_front();
                        executor->threads[std::this_thread::get_id()] = 1;
                        executor->busy_threads_count++;
                    }
                    if (executor->state != Executor::State::kRun) {
                        if (executor->threads[std::this_thread::get_id()] == 1) {
                            last_task = true; // это - если надо доделать последнюю задачу
                        } else {
                            return;
                        }
                    }
                } else { // просыпание по таймеру
                    if (executor->threads.size() <= executor->low_watermark) {
                        continue;
                    } else if (executor->threads.size() > executor->low_watermark) {
                        auto it = executor->threads.find(std::this_thread::get_id());
                        if (it != executor->threads.end()) {
                            executor->threads.erase(it);
                        }
                        return;
                    }
                }
            } else {
                task = executor->tasks.front();
                executor->tasks.pop_front();
                executor->threads[std::this_thread::get_id()] = 1;
                executor->busy_threads_count++;
            }
        }

        task();
        //        sleep(1); // for tests only
        {
            std::lock_guard<std::mutex> lg(executor->mutex);
            executor->threads[std::this_thread::get_id()] = 0;
            executor->busy_threads_count--;
            if (executor->busy_threads_count == 0) {
                executor->empty_condition.notify_all();
            }
        }
        if (last_task) {
            std::lock_guard<std::mutex> lg(executor->mutex);
            auto it = executor->threads.find(std::this_thread::get_id());
            if (it != executor->threads.end()) {
                executor->threads.erase(it);
            }
            return;
        }
    }
};

Executor::Executor(int l_w, int h_w, int m_q_s, std::chrono::milliseconds i_t)
    : low_watermark(l_w), hight_watermark(h_w), max_queue_size(m_q_s), idle_time(std::chrono::milliseconds(i_t)),
      state(State::kRun), busy_threads_count(0) {
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
    empty_condition.notify_all();
    {
        std::unique_lock<std::mutex> lock(mutex);
        while (!tasks.empty()) {
            empty_condition.wait(lock);
        }
    }
    if (await) {
        std::unique_lock<std::mutex> lock(mutex);
        while (busy_threads_count) {
            empty_condition.wait(lock);
        }
    }
    {
        std::lock_guard<std::mutex> lock(mutex);
        Executor::state = Executor::State::kStopped;
    }
    empty_condition.notify_all();
}

} // namespace Concurrency
} // namespace Afina
