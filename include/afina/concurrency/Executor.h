#ifndef AFINA_CONCURRENCY_EXECUTOR_H
#define AFINA_CONCURRENCY_EXECUTOR_H

#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace Afina {
namespace Concurrency {

class Executor;
void perform(Executor *executor);

/**
 * # Thread pool
 */
class Executor {

    enum class State {
        // Threadpool is fully operational, tasks could be added and get executed
        kRun,

        // Threadpool is on the way to be shutdown, no ned task could be added, but existing will be
        // completed as requested
        kStopping,

        // Threadppol is stopped
        kStopped
    };

public:
    friend void perform(Executor *executor);

    Executor(int l_w = 3, int h_w = 5, int m_q_s = 20, std::chrono::milliseconds i_t = std::chrono::milliseconds(1000));
    //    ~Executor();

    /**
     * Signal thread pool to stop, it will stop accepting new jobs and close threads just after each become
     * free. All enqueued jobs will be complete.
     *
     * In case if await flag is true, call won't return until all background jobs are done and all threads are stopped
     */
    void Stop(bool await = false);

    /**
     * Add function to be executed on the threadpool. Method returns true in case if task has been placed
     * onto execution queue, i.e scheduled for execution and false otherwise.
     *
     * That function doesn't wait for function result. Function could always be written in a way to notify caller about
     * execution finished by itself
     */
    //    template <typename F, typename... Types> bool Execute(F &&func, Types... args);
    template <typename F, typename... Types> bool Execute(F &&func, Types... args) {
        // Prepare "task"
        auto exec = std::bind(std::forward<F>(func), std::forward<Types>(args)...);

        std::unique_lock<std::mutex> lock(this->mutex);
        if (state != State::kRun) {
            return false;
        }

        // Enqueue new task
        if (tasks.size() < max_queue_size) {
            tasks.push_back(exec);
            if (busy_threads_count == threads.size() && threads.size() < hight_watermark) {
                auto new_thread = std::thread(perform, this);
                threads.insert(std::make_pair(new_thread.get_id(), 0));
                new_thread.detach();
            }
            empty_condition.notify_one();
            return true;
        } else {
            return false;
        }
    }

private:
    // No copy/move/assign allowed
    Executor(const Executor &);            // = delete;
    Executor(Executor &&);                 // = delete;
    Executor &operator=(const Executor &); // = delete;
    Executor &operator=(Executor &&);      // = delete;

    /**
     * Mutex to protect state below from concurrent modification
     */
    std::mutex mutex;

    /**
     * Conditional variable to await new data in case of empty queue
     */
    std::condition_variable empty_condition;

    /**
     * Map of actual threads that perform execution
     * value == 1, у тех процессов, которые на данный момент выполняют задачу
     */
    std::map<std::thread::id, int> threads;

    /**
     * Task queue
     */
    std::deque<std::function<void()>> tasks;

    /**
     * Flag to stop bg threads
     */
    State state;

    // минимальное количество потоков, которое должно быть в пуле
    int low_watermark;

    // максимальное количество потоков в пуле
    int hight_watermark;

    // максимальное число задач в очереди
    int max_queue_size;

    int busy_threads_count;

    // количество миллисекунд, которое каждый из поток ждет задач; по истечении должен быть убит и удален из пула
    std::chrono::milliseconds idle_time;
};

} // namespace Concurrency
} // namespace Afina

#endif // AFINA_CONCURRENCY_EXECUTOR_H
