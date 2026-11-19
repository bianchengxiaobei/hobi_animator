#include "anim/jobs/ThreadPoolJobScheduler.h"

#include <algorithm>

namespace anim {

ThreadPoolJobScheduler::ThreadPoolJobScheduler(int workerCount) {
    if (workerCount <= 0) {
        workerCount = static_cast<int>(std::thread::hardware_concurrency());
    }
    if (workerCount <= 0) workerCount = 1;

    _workers.reserve(static_cast<std::size_t>(workerCount));
    for (int i = 0; i < workerCount; ++i) {
        _workers.emplace_back([this] { workerLoop(); });
    }
}

ThreadPoolJobScheduler::~ThreadPoolJobScheduler() {
    waitAll();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _stopping = true;
    }
    _workCv.notify_all();
    for (std::thread& worker : _workers) {
        if (worker.joinable()) worker.join();
    }
}

std::shared_ptr<ThreadPoolJobScheduler::Task> ThreadPoolJobScheduler::find(
    JobHandle handle) const {
    const auto it = _tasks.find(handle.id);
    return it != _tasks.end() ? it->second : nullptr;
}

void ThreadPoolJobScheduler::enqueue(JobHandle handle) {
    const auto task = find(handle);
    if (task == nullptr || task->queued || task->done) return;
    task->queued = true;
    _queue.push_back(handle);
    _workCv.notify_one();
}

JobHandle ThreadPoolJobScheduler::schedule(std::function<void()> work,
                                           std::span<const JobHandle> dependencies) {
    if (!work) return JobHandle{};

    std::lock_guard<std::mutex> lock(_mutex);

    const JobHandle handle{++_lastId};
    auto task = std::make_shared<Task>();
    task->work = std::move(work);
    _tasks.emplace(handle.id, task);
    ++_unfinished;

    for (const JobHandle dep : dependencies) {
        if (!dep.valid()) continue;
        const auto it = _tasks.find(dep.id);
        if (it == _tasks.end() || it->second->done) continue;
        ++task->pending;
        it->second->dependents.push_back(handle);
    }

    if (task->pending.load(std::memory_order_relaxed) == 0) {
        enqueue(handle);
    }
    return handle;
}

void ThreadPoolJobScheduler::workerLoop() {
    for (;;) {
        JobHandle handle{};
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _workCv.wait(lock, [this] { return _stopping || !_queue.empty(); });
            if (_stopping && _queue.empty()) return;

            handle = _queue.front();
            _queue.pop_front();
        }

        const auto task = find(handle);
        if (task != nullptr && task->work) task->work();

        markDone(handle);
    }
}

void ThreadPoolJobScheduler::markDone(JobHandle handle) {
    std::vector<JobHandle> ready;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        const auto it = _tasks.find(handle.id);
        if (it == _tasks.end() || it->second->done) return;

        Task& task = *it->second;
        task.done = true;
        task.work = {};
        if (_unfinished > 0) --_unfinished;

        for (const JobHandle dependent : task.dependents) {
            const auto depIt = _tasks.find(dependent.id);
            if (depIt == _tasks.end() || depIt->second->done) continue;
            if (--depIt->second->pending == 0) ready.push_back(dependent);
        }

        for (const JobHandle readyHandle : ready) {
            enqueue(readyHandle);
        }
        if (_unfinished == 0) _doneCv.notify_all();
    }
    _doneCv.notify_all();
}

void ThreadPoolJobScheduler::complete(JobHandle handle) {
    if (!handle.valid()) return;
    std::unique_lock<std::mutex> lock(_mutex);
    _doneCv.wait(lock, [&] {
        const auto it = _tasks.find(handle.id);
        return it == _tasks.end() || it->second->done;
    });
}

void ThreadPoolJobScheduler::waitAll() {
    std::unique_lock<std::mutex> lock(_mutex);
    _doneCv.wait(lock, [this] { return _unfinished == 0; });
}

void ThreadPoolJobScheduler::parallelFor(
    std::size_t count, std::function<void(std::size_t begin, std::size_t end)> body) {
    if (!body || count == 0) return;

    const std::size_t workers =
        std::max<std::size_t>(1, static_cast<std::size_t>(workerCount()));
    const std::size_t chunks = std::min(count, workers);
    const std::size_t chunkSize = count / chunks;

    std::vector<JobHandle> handles;
    handles.reserve(chunks);

    for (std::size_t i = 0; i < chunks; ++i) {
        const std::size_t begin = i * chunkSize;
        const std::size_t end = (i + 1 == chunks) ? count : begin + chunkSize;
        handles.push_back(schedule([&body, begin, end] { body(begin, end); }));
    }

    waitAll();
}

}  // namespace anim
