#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>

using namespace std;

/*
 * 不直接使用enum，而是使用enum class的原因是
 * enum不通过枚举类名引用类型，多个枚举类型存在时，内部类型可能出现命名冲突。
 */
enum class PoolMode {
    MODE_FIXED,
    MODE_CACHED
};

class Any {
public:
    Any() = default;
    ~Any() = default;
    
    Any(const Any&) = delete;
    Any& operator = (const Any&) = delete;
    Any(Any&&) = default;
    Any& operator = (Any&&) = default;

    template<typename T>
    Any(T data) : base_ptr(make_unique<Derive<T>>(data)) {}

    template<typename T>
    T cast_() {
        Derive<T>* pd = dynamic_cast<Derive<T>*>(base_ptr.get());
        if(pd == nullptr) {
            throw "type is unmatch!";
        }
        return pd->data_;
    }

private:
    class Base {
    public:
        /*
        * 当存在基类指针指向派生类对象并想通过这个基类对象的指针删除派生类对象时，就应该将析构函数设计为虚函数
        * 如果基类的析构函数不是虚函数的话，删除派生对象时，只有基类对象的析构函数被调用，派生对象的析构函数没有被调用，从而导致内存泄漏问题
        * default以为着使用默认构造，相当于~Base(){}，但是编译器会对此进行优化
        */
        virtual ~Base() = default;
    };
    
    template<typename T>
    class Derive : public Base {
    public: 
        Derive(T data) : data_(data) {}
        T data_;
    };
private:
    unique_ptr<Base> base_ptr;
};

class Semaphore {
public: 
    Semaphore(int resource = 0) : resource_(resource) {}
    ~Semaphore() = default;

    void wait() {
        unique_lock<mutex> lock(semaphore_mutex);
        cv.wait(lock, [&]()->bool { return resource_ > 0; });
        resource_--;
    }

    void post() {
        unique_lock<mutex> lock(semaphore_mutex);
        resource_++;
        cv.notify_all();
    }
private:
    int resource_;
    mutex semaphore_mutex;
    condition_variable cv;
};

/*
template<typename T>
class MyAny {
private:
    MyAny(T data) : base_ptr(data) {}
public:
    template<typename T>
    class MyBase {
    public:
        MyBase(T data) : data_(data){}
    private:
        T data_;
    };
private:
    unique_ptr<MyBase> base_ptr;
};
*/

class Thread {
public:
    /*
    * 可以像函数一样使用可调用对象
    * 线程执行的任务由线程池分配，所以线程需要接收一个可调用线程对象
    * 该对象来自线程池的任务对象
    */
    using ThreadFunc = function<void()>;

    Thread(ThreadFunc func);

    ~Thread();
    void begin();

    /*
     * 获取线程ID  
    */
    int getId() const;
private:
    ThreadFunc func;
    static int fatherThreadId;
    int threadID;
};

/*
* 前置声明
*/
class Task;

class Result {
public:
    Result(shared_ptr<Task> task, bool isValid = true);
    ~Result() = default;

    void setAny(Any any);

    Any get();
private:
    Any anyres;
    Semaphore sem;
    shared_ptr<Task> task_;
    bool isValid_;
};

/*
* Task携带Result指针对象在submitTask提交到任务队列
* 任务被随机线程从队列取出，调用exec执行run并调用setAny将返回的Any赋给result，并用信号量通知完成赋值
* 
*/
class Task {
public:
    Task();
    ~Task() = default;

    /*
    * exec调用run但是比run做更多的事情，保持run的多态
    */
    void exec();
    virtual Any run() = 0;
    void setResult(Result* res);

private:
    Result* result;
};

/*
* example:
* ThreadPool pool;
* pool.start();
* class YourTask : public Task {
*   public:
*   void run() {}
* }
* pool.submitTask(std::make_shared<MyTask>);
*/
class ThreadPool {
public:
    /*
     *  构造函数
     */
    ThreadPool();

    /*
     * 设置线程池模式
     */
    void setMode(PoolMode mode);

    /*
     * 设置任务队列阈值
     */
    void setTaskCapacity(int capacity);

    /*
    * 设置线程数量阈值
    */
    void setThreadCapacity(int thread_capacity); 

    /*
     * 提交任务
     */
    Result submitTask(shared_ptr<Task> task);

    /*
     * 启动线程池
     */
    void start(int size);

    /*
     * 析构函数
     */
    ~ThreadPool();
    
    /*
    * 终止线程池
    */
    void stop() {
        
    }
    
    /*
     * 禁止拷贝构造,禁止拷贝赋值
     * 禁止这两个操作是因为该对象可能拥有锁、线程、文件句柄等不可复制且不应复制的资源
     * 拷贝这类资源可能会导致资源重复释放或者未定义行为
     */    
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator = (const ThreadPool&) = delete;
    
private:
    /*
     * 线程函数放在线程中可以方便访问线程池对象的私有成员变量
     */
    void threadFunc();

    /*
    * 检查运行状态
    */
    bool checkRunning() const;

private:
    /*
     * 线程列表
     *
     * 不使用裸指针，因为裸指针需要手动释放资源，否则会导致内存的泄漏
     * 依据RAII（资源获取即初始化）使用智能指针
     * 
     * RAII资源获取即初始化，将资源的生命周期和对象的生命周期进行绑定，
     * 对象初始化时获取资源，对象销毁时释放资源，通过这种手段来自动管理资源的生命周期
     * 避免内存泄漏
     * 
     * unique_ptr是一种独占所有权的智能指针，保证只有一个指向对象的指针。
     * 
     * shared_ptr是一种共享的所有权智能指针，可以有多个指向对象的指针，
     * 并跟踪记录指针，当所有指向对象的指针被销毁时，销毁对象。
     */
    vector<unique_ptr<Thread>> threads;

    /*
     * size_t 是一个无符号整数类型，它保证了代码在不同平台上的可移植性。
    */
    size_t initThreadSize;

    /*
    * 线程的最大阈值，用于避免Cached模式下，创建过多线程导致栈溢出
    */
    int threadCapacity;

    /*
     * 记录当前存在的线程 
    */
    int currentThreadSize;
    /*
     * CPP 对象的多态性只能通过指针或引用实现，所以想在队列中存储Task的派生对象，并通过基类指针访问它们，就必须使用指针和引用。
     * 同时在队列中存储Task对象需要消耗大量的内存，直接存储指针则可以节省内存。
     * 引用的缺点是不能重新指向。
     * 我们需要替API的使用者管理Task的生命周期，所以使用shared_ptr智能指针来管理对象的生命周期。
     */

    queue<shared_ptr<Task>> taskque;

    /*
     * 任务数量会被每个添加任务的对象调用，需要使用原子操作保障其线程安全。
     */
    atomic_uint taskSize;

    /*
     * 最大任务数量
     * 服务降级，避免过多任务提交导致内存溢出
     * 到达上限时，拒绝继续提交任务
     */
    int taskCapacity;
                           
    /*
     * 保证任务队列的线程安全
     */
    mutex taskqueMutex;

    /*
     * 两种队列状态对应两种条件变量
     */
    condition_variable notFull;
    condition_variable notEmpty;

    /*
     * 记录PoolMode
     */
    PoolMode poolMode;

    /*
    * 启动标识
    */
    bool isRunning;

    /*
    * 记录空闲线程
    */
    atomic_int idleThreadSize;
};

#endif