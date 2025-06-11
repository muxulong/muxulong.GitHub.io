#include <thread>
#include <mutex>
#include <vector>
#include <queue>
#include <atomic>
#include <functional>
#include <condition_variable>
#include <map>
#include <future>
#include <iostream>
using namespace std;

class ThreadPool
{
public:
    ThreadPool(int min = 4, int max = thread::hardware_concurrency());
    ~ThreadPool();
    void manager();
    void worker();
    void addTask(function<void(void)>);

private:
    thread *m_manager;
    atomic<int> m_minThreadNumber;
    atomic<int> m_maxThreadNumber;
    atomic<int> m_curThreadNumber;
    atomic<int> m_idlThreadNumber;
    atomic<int> m_exitThreadNumber;
    atomic<bool> m_stop;
    map<thread::id, thread> m_workers; // 任务队列
    vector<thread::id> m_idsVector;

    queue<function<void(void)>> m_taskQueue;
    mutex m_taskMutex;
    mutex m_queueMutex;
    condition_variable m_condition;

    /*
    thread *m_manager;
    map<thread::id, thread> m_workers;
    vector<thread::id> m_idsVector;
    int m_minThreads;
    int m_maxThreads;
    atomic<bool> m_stop;
    atomic<int> m_curThreads;
    atomic<int> m_idleThreads;
    atomic<int> m_exitNumber;
    queue<function<void()>> m_taskQueue;
    mutex m_idsMutex;
    mutex m_queueMutex;
    condition_variable m_condition;
    */
};

ThreadPool::ThreadPool(int min, int max) : m_maxThreadNumber(max),
                                           m_minThreadNumber(min), m_stop(false), m_exitThreadNumber(0)
{
    // m_idleThreads = m_curThreads = max / 2;
    m_idlThreadNumber = m_curThreadNumber = min;
    cout << "线程数量: " << m_curThreadNumber << endl;
    m_manager = new thread(&ThreadPool::manager, this);
    for (int i = 0; i < m_curThreadNumber; ++i)
    {
        thread t(&ThreadPool::worker, this);
        m_workers.insert(make_pair(t.get_id(), move(t)));
    }
}

ThreadPool::~ThreadPool()
{
    m_stop.store(true);
    m_condition.notify_all();
    for (auto &it : m_workers)
    {
        thread &t = it.second;
        if (t.joinable())
        {
            cout << "******** 线程 " << t.get_id() << " 将要退出了..." << endl;
            t.join();
        }
    }
    if (m_manager->joinable())
    {
        m_manager->join();
    }
    delete m_manager;
}

void ThreadPool::manager()
{
    while (!m_stop.load())
    {
        this_thread::sleep_for(chrono::seconds(2));
        int idle = m_idlThreadNumber.load();
        int current = m_curThreadNumber.load();
        if (idle > current / 2 && current > m_minThreadNumber)
        {
            m_exitThreadNumber.store(2);
            m_condition.notify_all();
            unique_lock<mutex> lck(m_taskMutex);
            for (const auto &id : m_idsVector)
            {
                auto it = m_workers.find(id);
                if (it != m_workers.end())
                {
                    cout << "############## 线程 " << (*it).first << "即将被销毁...." << endl;
                    (*it).second.join();
                    m_workers.erase(it);
                }
            }
            m_idsVector.clear();
        }
        else if (idle == 0 && current < m_maxThreadNumber)
        {
            thread t(&ThreadPool::worker, this);
            cout << "+++++++++++++++ 添加了一个线程, id: " << t.get_id() << endl;
            m_workers.insert(make_pair(t.get_id(), move(t)));
            m_curThreadNumber++;
            m_idlThreadNumber++;
        }
    }
}

void ThreadPool::addTask(function<void(void)> task)
{
    {
        lock_guard<mutex> locker(m_queueMutex);
        m_taskQueue.emplace(task);
    }
    m_condition.notify_one();
}

void ThreadPool::worker()
{
    while (!m_stop.load())
    {
        function<void(void)> task = nullptr;
        {
            unique_lock<mutex> locker(m_queueMutex);
            while (!m_stop.load() && m_taskQueue.empty())
            {
                m_condition.wait(locker);
                if (m_exitThreadNumber.load() > 0)
                {
                    cout << "----------------- 线程任务结束, ID: " << this_thread::get_id() << endl;
                    m_exitThreadNumber--;
                    m_curThreadNumber--;
                    unique_lock<mutex> lck(m_taskMutex);
                    m_idsVector.emplace_back(this_thread::get_id());
                    return;
                }
            }

            if (!m_taskQueue.empty())
            {
                cout << "取出一个任务..." << endl;
                task = move(m_taskQueue.front());
                m_taskQueue.pop();
            }
        }

        if (task)
        {
            m_idlThreadNumber--;
            task();
            m_idlThreadNumber++;
        }
    }
}

void calc(int x, int y)
{
    int res = x + y;
    cout << "res = " << res << endl;
    this_thread::sleep_for(chrono::seconds(2));
}

int main(int argc, char *argv[])
{
    ThreadPool pool(4);
    for (int i = 0; i < 10; ++i)
    {
        auto func = bind(calc, i, i * 2);
        pool.addTask(func);
    }
    getchar();
    return 0;
}