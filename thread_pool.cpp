#include <vector>
#include <iostream>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>

template<typename T>
class Singleton {
public:
    static T& get_instance() {
        static std::once_flag flag;
        std::call_once(flag, [&]() {
            // construct single instance from config file
            _instance = std::unique_ptr<T>(new T());
        });
        return *_instance;
    }
private:
    static std::unique_ptr<T> _instance;
};

template<typename T>
std::unique_ptr<T> Singleton<T>::_instance = nullptr;


class ThreadPool {
public:
    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>;
private:
    friend class std::default_delete<ThreadPool>;
    friend class Singleton<ThreadPool>;
    explicit ThreadPool(size_t cnt = std::thread::hardware_concurrency());
    ~ThreadPool();
    void routing();
public:
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

private:
    // need to keep track of threads, so we can join them
    std::vector<std::thread> _workers;
    // the task queue
    std::queue<std::function<void()>> _tasks;
    // synchronization
    mutable std::mutex _mtx;
    mutable std::condition_variable _cv;
    bool _stop;
};

void ThreadPool::routing() {
    std::function<void()> task;
    for(;;) {
        {
            std::unique_lock<std::mutex> lock(_mtx);
            // stop == false && task.empty() -> 阻塞
            _cv.wait(lock, [this] { return this->_stop || !this->_tasks.empty(); });

            // stop == true && task.empty() -> 结束线程
            if (_stop && _tasks.empty()) { return; }

            // !task.empty() -> 继续运行
            task = std::move(_tasks.front());
            _tasks.pop();
        }

        task();
    }
}

// the constructor just launches some amount of workers
inline ThreadPool::ThreadPool(size_t cnt) : _stop(false) {
    for(size_t i = 0; i < cnt ; ++i) _workers.emplace_back([this] { routing(); });
}

// add new work item to the pool
template<typename F, typename... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type> {
    using return_type = typename std::result_of<F(Args...)>::type;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> res = task->get_future();

    {
        std::unique_lock<std::mutex> lock(_mtx);

        // don't allow enqueueing after stopping the pool
        if(_stop) throw std::runtime_error("enqueue on stopped ThreadPool");

        _tasks.emplace([task] () { (*task)(); });
    }

    _cv.notify_one();
    return res;
}

// the destructor joins all threads
inline ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(_mtx);
        _stop = true;
    }
    _cv.notify_all();
    for(std::thread &worker: _workers) worker.join();
}

int main() {
    ThreadPool& pool = Singleton<ThreadPool>::get_instance();
    auto task = []() {
        std::cout << "Hello World!\n" << std::endl;
    };

    for(int i = 0; i < 1000; ++i) {
        pool.enqueue(task);
    }

    return 0;
}

