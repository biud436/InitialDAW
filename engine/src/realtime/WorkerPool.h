// 오디오 스레드가 블록마다 일을 나눠 맡기는 일꾼 무리. 일꾼은 세마포어로 깨어나 번호를 하나씩 집어 가고,
// 부른 스레드도 함께 일한다. 블록 안에서 할당하지 않고, 모든 일꾼이 쉬는 자리로 돌아온 뒤에야 돌아간다.
#pragma once

#include <atomic>
#include <cstddef>
#include <semaphore>
#include <thread>
#include <vector>

namespace mr
{

class WorkerPool
{
public:
    // workers 가 0 이면 부른 스레드가 혼자 한다
    explicit WorkerPool (int workers);
    ~WorkerPool();

    int workers() const noexcept { return (int) threads_.size(); }

    // fn(context, i) 를 i = 0 .. count-1 에 대해 부르고, 모두 끝나면 돌아온다
    void run (size_t count, void (*fn) (void*, size_t), void* context);

    // 쓸 만한 일꾼 수: 코어 수에서 하나를 빼고 8 까지
    static int defaultWorkers();

private:
    void loop();
    void work();

    std::vector<std::thread> threads_;
    std::counting_semaphore<64> start_ { 0 };
    std::atomic<size_t> next_ { 0 };
    std::atomic<size_t> done_ { 0 };
    std::atomic<int> idle_ { 0 };
    std::atomic<bool> quit_ { false };
    size_t count_ = 0;
    void (*fn_) (void*, size_t) = nullptr;
    void* context_ = nullptr;
};

} // namespace mr
