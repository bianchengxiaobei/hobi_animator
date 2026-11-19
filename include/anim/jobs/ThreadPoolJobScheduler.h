#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <unordered_map>
#include <vector>

#include "anim/jobs/JobScheduler.h"

// 并行任务调度后端（P7）。
//
// 文档：docs/16-任务调度与ECS集成.md §3.2、§5
// - 依赖用引用计数表达：前置任务完成后把后继的 pending 减到 0 时入队。
// - 同一角色的 job 仍有顺序依赖（由调用方按顺序提交/依赖），跨角色可并行。
// - parallelFor 把 [0,count) 按 workerCount 均分；每个分片是一个独立任务。
namespace anim {

class ThreadPoolJobScheduler final : public IJobScheduler {
public:
    explicit ThreadPoolJobScheduler(int workerCount = 0);
    ~ThreadPoolJobScheduler() override;

    ThreadPoolJobScheduler(const ThreadPoolJobScheduler&) = delete;
    ThreadPoolJobScheduler& operator=(const ThreadPoolJobScheduler&) = delete;

    JobHandle schedule(std::function<void()> work,
                       std::span<const JobHandle> dependencies = {}) override;
    void complete(JobHandle handle) override;
    void waitAll() override;

    void parallelFor(std::size_t count,
                     std::function<void(std::size_t begin, std::size_t end)> body) override;

    int workerCount() const override { return static_cast<int>(_workers.size()); }

private:
    struct Task {
        std::function<void()> work;
        std::atomic<int> pending{0};
        std::vector<JobHandle> dependents;
        bool done = false;
        bool queued = false;
    };

    void workerLoop();
    void enqueue(JobHandle handle);
    void markDone(JobHandle handle);
    std::shared_ptr<Task> find(JobHandle handle) const;

    mutable std::mutex _mutex;
    std::condition_variable _workCv;
    std::condition_variable _doneCv;
    std::deque<JobHandle> _queue;
    std::unordered_map<uint64_t, std::shared_ptr<Task>> _tasks;
    std::vector<std::thread> _workers;
    uint64_t _lastId = 0;
    uint32_t _unfinished = 0;
    bool _stopping = false;
};

}  // namespace anim
