#ifndef THREADPOOL_H
#define THREADPOOL_H
#include<pthread.h> //线程
#include<list> //用于队列
#include<exception> //抛出异常
#include<cstdio> //在C++中
#include"../lock/locker.h" //互斥锁
#include"../CGImysql/sql_connection_pool.h"

//模板类，实现代码的复用
template<typename T>//T就是线程处理的任务

//线程池类
class threadpool
{
private:
    //当把线程函数封装在类中，this指针会作为默认的参数被传进函数中，从而和线程函数参数(void*)不能匹配。
    //线程函数所以作为静态函数，因为在C++中静态函数没有this指针
    static void* worker(void *arg);//共有线程工作函数，静态函数
    void run();//私有线程工作函数线程创建后跑起来
private:
    //线程池中线程的数量
    int m_thread_number;
    //线程池数组，大小为线程数量m_thread_number
    pthread_t *m_threads;
    //请求队列中最多允许的、等待处理的请求数量
    int m_max_requests;
    //请求队列
    std::list<T*> m_workqueue;
    //互斥锁
    locker m_queuelocker;
    //信号量，用于判断是否有任务需要处理
    sem m_queuestat;
    //是否结束线程的标志
    bool m_stop;
    //数据库
    connection_pool *m_connPool;
    //模型切换
    int m_actor_model;
public:
    threadpool(int actor_model,connection_pool *connPool,int thread_number=8,int max_requests =100000);//线程数量，最大等待请求数量
    ~threadpool();
    bool append(T* request,int state);//添加任务方法
    bool append_p(T *request);
};

//线程池构造函数
template<typename T>
threadpool<T>::threadpool(int actor_model,connection_pool *connPool,int thread_number,int max_requests)://：后可以对成员进行初始化
    m_actor_model(actor_model),m_thread_number(thread_number),m_max_requests(max_requests),
    m_stop(false),m_threads(NULL),m_connPool(connPool){
        if((thread_number<=0)||(max_requests<=0)){//传入参数不对
            throw std::exception();
        }
        m_threads = new pthread_t[m_thread_number];//创建数组
        if(!m_threads){//创建失败
            throw std::exception();
        }

        // 创建thread_number个线程并将它们设置为线程脱离
        for(int i=0;i<thread_number;i++){
            printf("create the %d thread\n",i);
            if(pthread_create(m_threads+i,NULL,worker,this)!=0){//创建线程，this实现work静态函数调用局部变量
                delete[] m_threads;//创建失败把数组释放掉
                throw std::exception();
            }
            if(pthread_detach(m_threads[i])){//线程分离
                delete[] m_threads;
                throw std::exception();
            }
        }
    }
//线程池析构函数
template<typename T>
threadpool<T>::~threadpool(){
    delete[] m_threads;
    m_stop=true;
}
//向队列中添加任务
template<typename T>
bool threadpool<T>::append(T *request,int state){
    m_queuelocker.lock();
    if(m_workqueue.size()>m_max_requests){
        m_queuelocker.unlock();//已超过最大工作数量，解锁
        return false;
    }
    request->m_state=state;
    m_workqueue.push_back(request);//添加到工作队列中
    m_queuelocker.unlock();
    m_queuestat.post();//信号量加1
    return true;
}
template <typename T>
bool threadpool<T>::append_p(T *request)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}
//创建工作线程
template<typename T>
void* threadpool<T>::worker(void *arg){
    threadpool *pool=(threadpool *)arg;//静态函数调用局部变量
    pool->run();
    return pool;
}
//运行线程池中的线程
template<typename T>
void threadpool<T>::run(){
    while(!m_stop){//线程一直循环直到停止
        m_queuestat.wait();//信号量减1
        m_queuelocker.lock();
        if(m_workqueue.empty()){
            m_queuelocker.unlock();
            continue;
        }

        //有数据
        T* request=m_workqueue.front();//取队首
        m_workqueue.pop_front();//取完删除
        m_queuelocker.unlock();
        if(!request){//没获取到
            continue;
        }
        //Reactor
        if(m_actor_model==1){
            //IO事件类型：0为读
            if(request->m_state==0){
                if(request->read_once()){
                request->improv=1;
                connectionRAII mysqlcon(&request->mysql,m_connPool);
                request->process();//任务运行
                }
                else{
                    request->improv=1;
                    request->timer_flag=1;
                }
            }
            //写
            else{
                if(request->write()){
                    request->improv=1;
                }
                else{
                    request->improv=1;
                    request->timer_flag=1;
                }
            }
        }
    //模拟Proactor:线程池不需要数据读取，直接开始业务处理
    //之前的操作已经将数据读取到http的read和write的buffer中
    else{
        connectionRAII mysqlcon(&request->mysql,m_connPool);
        request->process();//任务运行
    }
    }
}
#endif