#pragma once

#include <atomic>
#include <cstddef>

namespace gb {

// Single-producer single-consumer queue, safe between two cores without locks.
template <typename T, size_t N>
class SpscQueue {
public:
    bool push(const T& value) {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next = (head + 1) % N;
        if (next == tail_.load(std::memory_order_acquire)) return false;
        items_[head] = value;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T& value) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return false;
        value = items_[tail];
        tail_.store((tail + 1) % N, std::memory_order_release);
        return true;
    }

private:
    T items_[N];
    std::atomic<size_t> head_{0};
    std::atomic<size_t> tail_{0};
};

}  // namespace gb
