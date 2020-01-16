#include <afina/concurrency/Executor.h>
#include <iostream>
#include <unistd.h>

namespace Afina {
namespace Concurrency {

void perform(Executor *executor) {
    std::function<void()> task;

    for (;;) {
        {
            std::unique_lock<std::mutex> lock(executor->mutex);
//                                    std::cout << executor->busy_threads_count << " " << executor->threads_count <<
//                                    std::endl;
            if (executor->empty_condition.wait_for(lock, executor->idle_time, [executor] {
                    return (!(executor->tasks.empty()) || (executor->state != Executor::State::kRun));
                })) { // просыпание по условию
                if (!executor->tasks.empty()) {
                    task = executor->tasks.front();
                    executor->tasks.pop_front();
                    executor->busy_threads_count++;
                }
                if (executor->state != Executor::State::kRun && executor->tasks.empty()) {
                    executor->threads_count--;
                    if (executor->threads_count == 0) {
                        executor->state = Executor::State::kStopped;
                        executor->await_condition.notify_all();
                    }
                    return;
                }
            }
            else if (executor->threads_count > executor->low_watermark) {
                executor->threads_count--;
                if (executor->threads_count == 0 && executor->state != Executor::State::kRun) {
                    executor->state = Executor::State::kStopped;
                    executor->await_condition.notify_all();
                }
                return;
            }
//          else if (executor->threads_count <= executor->low_watermark) {
//                    continue;
//          }
            else {;}
        }

        try {
            task();
        }
        catch(...){ }
//                        sleep(1); // for tests only
        {
            std::lock_guard<std::mutex> lg(executor->mutex);
            executor->busy_threads_count--;
        }
    }
};

Executor::Executor(int _low_watermark, int _hight_watermark, int _max_queue_size, std::chrono::milliseconds _idle_time)
    : low_watermark(_low_watermark), hight_watermark(_hight_watermark), max_queue_size(_max_queue_size),
      idle_time(std::chrono::milliseconds(_idle_time)), state(State::kRun), busy_threads_count(0) {
    for (int i = 0; i < low_watermark; ++i) {
        std::thread(perform, this).detach();
        threads_count++;
    }
}

// почему при реализации Executor::Execute(...) здесь, код не компилится ?

void Executor::Stop(bool await) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (Executor::state != Executor::State::kRun) {
            return;
        }
        else if (threads_count == 0) {
            Executor::state = Executor::State::kStopped;
            return;
        }
        else {
            Executor::state = Executor::State::kStopping;
        }
    }
    empty_condition.notify_all();

    if (await) {
        std::unique_lock<std::mutex> lock(mutex);
        while (threads_count > 0) {
            await_condition.wait(lock);
        }
    }
}

} // namespace Concurrency
} // namespace Afina
