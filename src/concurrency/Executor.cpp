#include <afina/concurrency/Executor.h>
#include <iostream>
#include <unistd.h>

namespace Afina {
namespace Concurrency {

void perform(Executor *executor) {
    std::function<void()> task;
    bool last_task = false;
    bool with_task;

    for (;;) {
        with_task = false;
        {
            std::unique_lock<std::mutex> lock(executor->mutex);
            //                        std::cout << executor->busy_threads_count << " " << executor->threads_count <<
            //                        std::endl;
            if (executor->tasks.empty()) {
                if (executor->empty_condition.wait_for(lock, std::chrono::milliseconds(100), [executor] {
                        return (!(executor->tasks.empty()) || (executor->state != Executor::State::kRun));
                    })) { // просыпание по условию
                    if (!executor->tasks.empty()) {
                        task = executor->tasks.front();
                        executor->tasks.pop_front();
                        with_task = true;
                        executor->busy_threads_count++;
                        if (executor->tasks.empty()) {
                            executor->stop_condition.notify_all();
                        }
                    }
                    if (executor->state != Executor::State::kRun) {
                        if (with_task) {
                            last_task = true; // надо выполнить task перед выходом
                        } else {
                            executor->threads_count--;
                            return;
                        }
                    }
                } else { // просыпание по таймеру
                    if (executor->threads_count <= executor->low_watermark) {
                        continue;
                    } else if (executor->threads_count > executor->low_watermark) {
                        executor->threads_count--;
                        return;
                    }
                }
            } else {
                task = executor->tasks.front();
                executor->tasks.pop_front();
                executor->busy_threads_count++;
                if (executor->tasks.empty()) {
                    executor->stop_condition.notify_all();
                }
            }
        }

        task();
        //                sleep(1); // for tests only
        {
            std::lock_guard<std::mutex> lg(executor->mutex);
            executor->busy_threads_count--;
            if (executor->busy_threads_count == 0) {
                executor->await_condition.notify_all();
            }
        }
        if (last_task) {
            std::lock_guard<std::mutex> lg(executor->mutex);
            executor->threads_count--;
            return;
        }
    }
};

Executor::Executor(int l_w, int h_w, int m_q_s, std::chrono::milliseconds i_t)
    : low_watermark(l_w), hight_watermark(h_w), max_queue_size(m_q_s), idle_time(std::chrono::milliseconds(i_t)),
      state(State::kRun), busy_threads_count(0) {
    for (int i = 0; i < low_watermark; ++i) {
        auto new_thread = std::thread(perform, this);
        threads_count++;
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
            stop_condition.wait(lock);
        }
    }
    if (await) {
        std::unique_lock<std::mutex> lock(mutex);
        while (busy_threads_count) {
            await_condition.wait(lock);
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
