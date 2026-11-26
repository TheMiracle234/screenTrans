#include <mutex>
#include <condition_variable>
#include <cstddef>
#include <utility>

// 有界信号量：计数 ∈ [0, max_value]
class ClampedSemaphore {
private:
    std::mutex mtx;
    std::condition_variable cv;
    ptrdiff_t count{ 0 };
    ptrdiff_t max;

public:
    explicit ClampedSemaphore(ptrdiff_t initial, ptrdiff_t max_val)
        : count(initial), max(max_val)
    {
    }

    // 增加n，最多顶到max，超过的部分直接丢弃
    void release(ptrdiff_t n = 1)
    {
        std::lock_guard<std::mutex> lock(mtx);
        if (count >= max)
            return;

        count += n;
        if (count > max)
            count = max;
        cv.notify_all();
    }

    // 阻塞获取，count>0才可以拿
    void acquire()
    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [this] { return count > 0; });
        --count;
    }

    // 非阻塞尝试获取
    bool try_acquire()
    {
        std::lock_guard<std::mutex> lock(mtx);
        if (count <= 0)
            return false;
        --count;
        return true;
    }

    // 仅调试快照，不能用来做TOCTOU判断
    ptrdiff_t current()
    {
        std::lock_guard<std::mutex> lock(mtx);
        return count;
    }

    // -------------------------- 拷贝 / 移动 --------------------------
    // mutex / condition_variable 不可拷贝、不可移动，所以整个类禁止拷贝、移动
    ClampedSemaphore(const ClampedSemaphore&) = delete;
    ClampedSemaphore& operator=(const ClampedSemaphore&) = delete;

    ClampedSemaphore(ClampedSemaphore&&) = delete;
    ClampedSemaphore& operator=(ClampedSemaphore&&) = delete;
};
