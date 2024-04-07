#ifndef SYNCSTACK_H
#define SYNCSTACK_H

#include <stack>
#include <mutex>
#include <condition_variable>

template <typename T>
class SyncStack {
private:
    std::stack<T> m_stack;
    std::mutex mutex;
    std::condition_variable cond;

public:
    void push(T const& value) {
        std::unique_lock<std::mutex> lock(this->mutex);
        m_stack.push(value);
        lock.unlock();
        this->cond.notify_one();
    }

    T pop() {
        std::unique_lock<std::mutex> lock(this->mutex);
        while(m_stack.empty()) {
            this->cond.wait(lock);
        }
        T rc(std::move(this->m_stack.top()));
        this->m_stack.pop();
        return rc;
    }
    int32_t size() {
        std::unique_lock<std::mutex> lock(this->mutex);
        return this->m_stack.size();
    }
};

#endif // SYNCSTACK_H