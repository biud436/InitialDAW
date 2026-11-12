#include "realtime/WorkerPool.h"

#include <algorithm>

#if defined(__APPLE__)
 #include <pthread.h>
#endif

namespace mr
{

int WorkerPool::defaultWorkers()
{
    const int cores = (int) std::thread::hardware_concurrency();
    return std::clamp (cores - 1, 0, 8);
}

WorkerPool::WorkerPool (int workers)
{
    workers = std::clamp (workers, 0, 32);
    for (int i = 0; i < workers; ++i)
        threads_.emplace_back ([this] { loop(); });
}

WorkerPool::~WorkerPool()
{
    quit_.store (true);
    start_.release ((std::ptrdiff_t) threads_.size());
    for (auto& t : threads_)
        t.join();
}

void WorkerPool::loop()
{
   #if defined(__APPLE__)
    pthread_set_qos_class_self_np (QOS_CLASS_USER_INTERACTIVE, 0);   // 오디오 스레드를 기다리게 하지 않도록 높은 우선순위
   #endif
    for (;;)
    {
        start_.acquire();
        if (quit_.load())
            return;
        work();
        idle_.fetch_add (1, std::memory_order_release);
    }
}

void WorkerPool::work()
{
    for (;;)
    {
        const size_t i = next_.fetch_add (1, std::memory_order_acq_rel);
        if (i >= count_)
            return;
        fn_ (context_, i);
        done_.fetch_add (1, std::memory_order_release);
    }
}

void WorkerPool::run (size_t count, void (*fn) (void*, size_t), void* context)
{
    if (threads_.empty() || count <= 1)
    {
        for (size_t i = 0; i < count; ++i)
            fn (context, i);
        return;
    }
    fn_ = fn;
    context_ = context;
    count_ = count;
    next_.store (0, std::memory_order_relaxed);
    done_.store (0, std::memory_order_relaxed);
    idle_.store (0, std::memory_order_relaxed);
    start_.release ((std::ptrdiff_t) threads_.size());   // 위의 값들이 일꾼에게 보인다 (release-acquire)
    work();
    while (done_.load (std::memory_order_acquire) < count)
        std::this_thread::yield();
    // 모든 일꾼이 빠져나온 뒤에 돌아가야 다음 블록이 값을 바꿀 수 있다
    while (idle_.load (std::memory_order_acquire) < (int) threads_.size())
        std::this_thread::yield();
}

} // namespace mr
