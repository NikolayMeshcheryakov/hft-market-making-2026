#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <optional>

namespace cmf
{

// ── Lock-free Single Producer Single Consumer Queue ───────────────────────
//
// One thread calls push() — the producer.
// One thread calls pop()  — the consumer.
// No locks. Uses atomic head/tail with cache-line padding to avoid
// false sharing between producer and consumer.
//
// Capacity must be a power of 2.
//
// Usage:
//   SPSCQueue<OrderMsg> q;
//   q.push(msg);           // producer thread
//   auto opt = q.pop();    // consumer thread
//   if (opt) process(*opt);

template <typename T, std::size_t Capacity = 4096>
class SPSCQueue
{
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be power of 2");
    static constexpr std::size_t kMask = Capacity - 1;

  public:
    SPSCQueue() noexcept : head_(0), tail_(0) {}

    // Producer: returns false if queue is full (never blocks)
    bool push(const T& item) noexcept
    {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t next = (tail + 1) & kMask;

        if (next == head_.load(std::memory_order_acquire))
            return false; // full

        buffer_[tail] = item;
        tail_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer: returns nullopt if queue is empty (never blocks)
    std::optional<T> pop() noexcept
    {
        const std::size_t head = head_.load(std::memory_order_relaxed);

        if (head == tail_.load(std::memory_order_acquire))
            return std::nullopt; // empty

        T item = buffer_[head];
        head_.store((head + 1) & kMask, std::memory_order_release);
        return item;
    }

    bool empty() const noexcept
    {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    std::size_t size() const noexcept
    {
        const std::size_t t = tail_.load(std::memory_order_acquire);
        const std::size_t h = head_.load(std::memory_order_acquire);
        return (t - h) & kMask;
    }

    static constexpr std::size_t capacity() noexcept { return Capacity; }

  private:
    // Separate cache lines for head and tail to avoid false sharing
    alignas(64) std::atomic<std::size_t> head_;
    alignas(64) std::atomic<std::size_t> tail_;
    std::array<T, Capacity> buffer_;
};

} // namespace cmf
