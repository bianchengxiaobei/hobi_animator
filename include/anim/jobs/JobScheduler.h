#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace anim {

struct JobHandle {
    uint64_t id = 0;

    bool valid() const noexcept { return id != 0; }
    bool operator==(const JobHandle&) const = default;
};

class IJobScheduler {
public:
    virtual ~IJobScheduler() = default;

    virtual JobHandle schedule(std::function<void()> work,
                               std::span<const JobHandle> dependencies = {}) = 0;
    virtual void complete(JobHandle handle) = 0;
    virtual void waitAll() = 0;

    virtual void parallelFor(std::size_t count,
                             std::function<void(std::size_t begin, std::size_t end)> body) = 0;

    virtual int workerCount() const = 0;
};

class InlineJobScheduler : public IJobScheduler {
public:
    JobHandle schedule(std::function<void()> work,
                       std::span<const JobHandle> /*dependencies*/ = {}) override {
        if (work) work();
        return JobHandle{++_lastId};
    }

    void complete(JobHandle /*handle*/) override {}
    void waitAll() override {}

    void parallelFor(std::size_t count,
                     std::function<void(std::size_t begin, std::size_t end)> body) override {
        if (body && count > 0) body(0, count);
    }

    int workerCount() const override { return 1; }

private:
    uint64_t _lastId = 0;
};

}  // namespace anim
