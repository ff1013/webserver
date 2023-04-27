#ifndef LOCKER_H//if not define
#define LOCKER_H
#include<pthread.h> //线程
#include<exception> //异常错误
#include<semaphore.h> //信号量
//线程同步机制封装类

//互斥锁类                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    
class locker
{
private:
    pthread_mutex_t m_mutex;//互斥锁
public:
    //构造函数
    locker(){
    if(pthread_mutex_init(&m_mutex,NULL)!=0){
        throw std::exception();//抛出异常对象
    }
    }
    //析构函数，做销毁工作
    ~locker(){
        pthread_mutex_destroy(&m_mutex);
    }
    //上锁
    bool lock(){
        return pthread_mutex_lock(&m_mutex)==0;
    }
    //解锁
    bool unlock(){
        return pthread_mutex_unlock(&m_mutex)==0;
    }
    //获取指针
    pthread_mutex_t *get(){
        return &m_mutex;
    }
};

//条件变量类
class cond
{
private:
    pthread_cond_t m_cond;
public:
    cond(){
        if(pthread_cond_init(&m_cond,NULL)!=0){
            throw std::exception();
        }
    }
    ~cond(){
        pthread_cond_destroy(&m_cond);
    }
    //等待条件变量的信号
    bool wait(pthread_mutex_t *mutex){
        return pthread_cond_wait(&m_cond,mutex)==0;
    }
    //超时
    bool timewait(pthread_mutex_t *mutex,struct timespec t){
        return pthread_cond_timedwait(&m_cond,mutex,&t)==0;
    }
    //条件变量增加，唤醒一个或多个线程
    bool signal(){
        return pthread_cond_signal(&m_cond);
    }
    //全部线程都唤醒
    bool broadcast(){
        return pthread_cond_broadcast(&m_cond);
    }
};

//信号量类
class sem
{
private:
    sem_t m_sem;
public:
    sem(){
        if(sem_init(&m_sem,0,0)!=0){
            throw std::exception();
        }
    }
    sem(int num){//一开始就赋值
        if(sem_init(&m_sem,0,num)!=0){
            throw std::exception();
        }
    }
    ~sem(){
        sem_destroy(&m_sem);
    }
    bool wait(){
        return sem_wait(&m_sem)==0;
    }
    bool post(){
        return sem_post(&m_sem)==0;
    }
};



#endif