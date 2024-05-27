#ifndef BITBI_UTIL_THREADPOOL_H
#define BITBI_UTIL_THREADPOOL_H
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

class ThreadPool {
public:
    ThreadPool() {
        ThreadPool(std::thread::hardware_concurrency());
    }
    ThreadPool(size_t numThreads) {
        for (size_t i = 0; i < numThreads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;

                    {
                        std::unique_lock<std::mutex> lock(this->queueMutex);
                        this->condition.wait(lock, [this] { return this->stop || !this->tasks.empty(); });
                        if (this->stop && this->tasks.empty()) return;
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }

                    try {
                        task();
                    } catch (...) {
                        std::unique_lock<std::mutex> lock(this->queueMutex);
                        exceptions.push_back(std::current_exception());
                        stop = true;
                        condition.notify_all();
                        return;
                    }
                }
            });
        }
    }

    ~ThreadPool() {
        stopAndWait();
    }

    template<class F>
    void enqueue(F&& f) {
        {
            std::unique_lock<std::mutex> lock(queueMutex);

            if (stop) {
                throw std::runtime_error("ThreadPool is stopped");
            }

            tasks.emplace(std::forward<F>(f));
        }
        condition.notify_one();
    }

    void stopAndWait() {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            if (stop) {
                return;
            }
            stop = true;
        }
        condition.notify_all();
        for (std::thread &worker : workers) worker.join();

        for (auto& e : exceptions) {
            if (e) {
                std::rethrow_exception(e);
            }
        }
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::vector<std::exception_ptr> exceptions;
    std::mutex queueMutex;
    std::condition_variable condition;
    bool stop = false;
};

#endif