#include "threadpool.hpp"
#include <thread>
#include <iostream>
#include <mutex>
#include <chrono>

using namespace std;

const int TASK_MAX_THREADPOOL = 1024;

/*
 * 默认线程池模式为固定大小
 * 初始化线程数量，任务数量、线程池阈值
 */

ThreadPool::ThreadPool()
    :initThreadSize(0)
     ,taskSize(0)
     ,taskCapacity(TASK_MAX_THREADPOOL)
     ,poolMode(PoolMode::MODE_FIXED)
{}

ThreadPool::~ThreadPool() {
    
}

void ThreadPool::setMode(PoolMode mode) {
    poolMode = mode;
}

void ThreadPool::setTaskCapacity(int capacity) {
    taskSize = capacity;
}

/*
* 生产者
*
* Result不能依赖于Task，因为Result是任务Task执行结束后才拿到的返回值
* 
* Task是放到队列中被随机线程执行的，那么任务的提交者怎么获取到Result呢
*/
Result ThreadPool::submitTask(shared_ptr<Task> task) {
    /*
    * 任务队列存在竟态条件需要加锁
    */
    unique_lock<mutex> lock(taskqueMutex);

    // while (taskSize == taskCapacity){notFull.wait_for(lock, chrono::seconds(1));}

    /*
    * 提交任务需要等待队列不满
    * 等待锁与条件，条件满足则从等待状态->阻塞状态，拿到锁则从阻塞状态->允许状态
    * 超时返回
    */
    notFull.wait_for(lock, chrono::seconds(1), [&]()->bool { return taskCapacity > taskSize; });

    /*
    * 放入任务
    */
    taskque.emplace(task);
    taskSize++;

    /*
    * 通知队列不空
    */
    notEmpty.notify_all();

}

void ThreadPool::start(int size = 4) {
    initThreadSize = size; 

    for(int i = 0; i < initThreadSize; i++) {
        /*
        * C++14 make_unique<Thread>创建独占智能指针
        */
        unique_ptr<Thread> thread_ptr = make_unique<Thread>(bind(&ThreadPool::threadFunc, this));

        /*
         * 左值代表的是对象本身，也意味着它有一个可以访问的内存地址，可以出现在赋值运算符的左边和右边
         * 右值代表的是对象的值，或者一个即将结束生命周期的对象，右值只能出现在赋值运算符的右边
         * 使用move函数可以将右值转换为左值，这样可以方便进行资源转移而不用复制资源
         * 
         * 复制操作：复制操作创建一个新对象，并将原对象的状态复制到新对象，这会涉及到资源的分配，以及数据的拷贝
         * 移动操作：移动操作将原对象的状态转移到新对象，而不需要复制数据，原对象通常会被置空
         * 移动操作性能比复制操作好，因为不涉及数据的拷贝
         * 
         * emplace_back和push_back都是往容器的末尾添加新元素，但emplace_back性能要更佳
         * push_back接收已经构造好的对象，并将其复制（已经存在的对象）和移动（临时对象）到容器的末尾
         * emplace_back可以接收参数直接在尾部构造一个新的对象，可以避免复制和拷贝
         * 
         * unique_ptr是独占智能指针，其对象只允许一个指向的指针，所以不允许复制，只允许移动
         * move将左值转换为右值，这样可以通过移动操作进行所有权转移
         */
        threads.emplace_back(move(thread_ptr));
    }
    
    for(int i = 0; i < initThreadSize; i++) {
        threads[i]->begin();
    }
}

void ThreadPool::threadFunc() {
    for(;;) {
        shared_ptr<Task> task;
        /*
        * 减轻锁重量，避免等待任务执行完毕再释放锁
        */
        {
            /* 
            * 获取锁
            */
            unique_lock<mutex> lock(taskqueMutex);

            /*
            * 等待条件变量
            * 被唤醒->获取锁->判断条件变量是否满足->继续执行
            */
            notEmpty.wait(lock, [&]()->bool{ return taskSize > 0; });

            /*
            * 条件满足消费任务
            */
            task = taskque.front();
            taskque.pop();
            taskSize--;

            // 可能是多余的
            // if(taskSize > 0) {
            //     notEmpty.notify_all();
            // }

            /*
            * 通知生产者队列不满
            */
            notFull.notify_all();

        } // 释放锁

        /*
        * 执行任务
        */
        if(task != nullptr) {
            task->exec();
        }
    }
}

Thread::Thread(ThreadFunc fc)
    :func(fc)
{}

Thread::~Thread() {
    /*
    * 不应该手动调用析构函数，对象生命周期将要结束时会，会自动调用析构函数进行资源清理。
    * 
    * 对象的生命周期会在以下几种情况下结束
    * 1. 离开作用域的时候
    * 2. 删除动态分配的对象
    * 3. 销毁容器时，容器内部全部对象的生命周期都会结束
    * 
    * ThreadPool pool 和 ThreadPool* heapPool = new ThreadPool()的区别
    * 1. pool在栈上分配内存，heapPool在堆上分配内存
    * 2. pool在离开作用域时生命周期结束，会被自动销毁，heapPool需要delete手动结束生命周期，否则会导致内存泄露
    * 3. pool在编译时就分配内存，heapPool会在运行时动态分配
    * 
    * 栈内存和堆内存的区别
    * 1. 生命周期：栈内存的生命周期由作用域决定，堆内存的生命周期由程序员决定需要用new分配堆内存，用delete来释放
    * 2. 大小：栈内存的大小在编译时确定，堆内存的大小可以在运行时动态分配
    * 3. 性能：栈内存访问速度比堆内存更快，因为栈一般是连续分配的，堆的内存比较零散
    * 4. 灵活性：堆更灵活，可以动态分配
    * 5. 内存管理：栈在离开作用域会被销毁，不会导致内存泄漏，堆需要手动释放，否则会导致内存泄漏
    */
}

void Thread::begin() {
    /*
    * 创建线程对象
    */
    thread t(func);
    
    /*
    * 将线程对象和线程分离
    * 线程的运行独立于线程对象，线程对象的销毁不会影响程序的继续执行
    */
    t.detach();x
}

Result::Result(shared_ptr<Task> task, bool isValid = true)
    : task_(task), isValid_(isValid) {
        task_->setResult(this);
    }

Any Result::get() {
    if(!isValid_) {
        return "";
    }

    sem.wait();
    return move(anyres);
}

void Result::setAny(Any any) {
    anyres = move(any);
    sem.post();
}

Task::Task() 
    :result(nullptr)
{}

void Task::exec() {
    if(result == nullptr) { return; }
    result->setAny(run());
}

void Task::setResult(Result* res) {
    result = res;
}