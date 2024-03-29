# ThreadPoolUtil
能够接受任何函数并提供任意类型返回值的双模式线程池

## 使用说明

example:

创建一个新的源文件，以`test.cpp`为例，你可以取任何你喜欢的名字。

```cpp
# include "threadpool.hpp"
/*
* 创建一个类继承Task基类并重写run方法
*/
class MyTask : public Task{
public:
    /*
    * 可以自定义MyTask的构造函数
    */
    MyTask(int begin, int end)
        : begin_(begin), end_(end)
    {}
    Any run() {
        int sum;
        for(int i = begin_; i < end_; i++) {
            sum += i;
        }
        return sum;
    }
private:
    int begin_;
    int end_;
}

int main() {
    /*
    * 创建线程池
    */
    ThreadPool pool;
    /*
    * 启动线程池时可以自定义线程数量
    */
    pool.start(4);
    /*
    * Result::submitTask(shared_ptr<Task>());
    * 使用Result接收任意类型返回值
    */
    Result res1 = pool.submitTask(make_shared<MyTask>(0, 100));
    Result res2 = pool.submitTask(make_shared<MyTask>(101, 200));
    Result res2 = pool.submitTask(make_shared<MyTask>(101, 200));
    Result res2 = pool.submitTask(make_shared<MyTask>(101, 200));
    /*
    * 使用Result::get().cast_<T>()进行类型转换
    */
    int result = res1.get().cast_<int>() + res2.get().cast_<int>() + res3.get().cast_<int>() + res4.get().cast_<int>();
}
```

## 编译
编译前确保编译器支持C++20，运行下面命令进行编译。其中`test.cpp`是

```shell
g++ -std=c++2a test.cpp threadpool.cpp -o test -pthread
```
