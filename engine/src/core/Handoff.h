// 메시지 스레드가 만든 값을 오디오 스레드에 넘기는 자리. 오디오 스레드는 옛것을 retired 에 내려놓고, 메시지 스레드가 지운다.
#pragma once

#include <atomic>
#include <memory>

namespace mr
{

template <typename T>
class Handoff
{
public:
    Handoff() = default;
    Handoff (const Handoff&) = delete;
    Handoff& operator= (const Handoff&) = delete;
    ~Handoff()
    {
        delete next_.exchange (nullptr);
        delete retired_.exchange (nullptr);
    }

    // 메시지 스레드: 아직 가져가지 않은 앞의 값은 버린다
    void post (std::unique_ptr<T> value) { delete next_.exchange (value.release(), std::memory_order_acq_rel); }
    void collect() { delete retired_.exchange (nullptr, std::memory_order_acq_rel); }

    // 오디오 스레드: 새 값 (없거나 옛것을 아직 지우지 않았으면 nullptr). 가져갔으면 옛것을 retire 로 내려놓는다.
    T* take()
    {
        if (next_.load (std::memory_order_acquire) == nullptr || retired_.load (std::memory_order_acquire) != nullptr)
            return nullptr;
        return next_.exchange (nullptr, std::memory_order_acq_rel);
    }
    void retire (T* old) { retired_.store (old, std::memory_order_release); }

private:
    std::atomic<T*> next_ { nullptr };
    std::atomic<T*> retired_ { nullptr };
};

} // namespace mr
