#ifndef WEBSERVER_H
#define WEBSERVER_H

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<errno.h>//错误号相关
#include<fcntl.h>//文件描述符
#include<signal.h>//信号相关
#include<sys/epoll.h>
#include<cassert>

#include"../threadpool/threadpool.h"
#include"../http_conn/http_conn.h"

#define MAX_FD 65535 //最大文件描述符个数
#define MAX_EVENT_NUMBER 10000//监听的最大事件个数
const int TIMESLOT=5;//最小超时时间

class WebServer{
public:
    WebServer();
    ~WebServer();

    void init(int port,string user,string passWord,string databaseName,
    int log_write,int opt_linger,int trigmode,int sql_num,
    int thread_num,int close_log,int actor_model);
    void thread_pool();//线程池部分
    void sql_pool();//连接池部分
    void log_write();//日志部分
    void trig_mode();//epoll的触发模式
    void eventListen();//epoll监听部分
    void eventLoop();//事件回环（即服务器主线程）
    void timer(int connfd,struct sockaddr_in client_address);//定时器
    void adjust_timer(util_timer *timer);//调整定时器
    void deal_timer(util_timer *timer,int sockfd);//删除定时器
    bool dealclinetdata();//http处理客户端数据
    bool dealwithsignal(bool& timeout, bool& stop_server);//处理定时器信号
    void dealwithread(int sockfd);//处理读
    void dealwithwrite(int sockfd);//处理写

public:
    //基础
    int m_port;
    char *m_root;
    int m_log_write;
    int m_close_log;//是否关闭日志
    int m_actormodel;//并发模型选择：Reactor模式、Proactor模式

    int m_pipefd[2];//管道sockect,[0]读端,[1]写端
    int m_epollfd;
    http_conn *users;

    //数据库相关
    connection_pool *m_connPool;
    string m_user;         //登陆数据库用户名
    string m_passWord;     //登陆数据库密码
    string m_databaseName; //使用数据库名
    int m_sql_num;

    //线程池相关
    threadpool<http_conn> *m_pool;
    int m_thread_num;//线程池数量

    //epoll_event相关
    epoll_event events[MAX_EVENT_NUMBER];

    int m_listenfd;
    int m_OPT_LINGER;//是否优雅关闭连接
    int m_TRIGMode;//触发组合模式
    int m_LISTENTrigmode;//listenfd的触发模式，listenfd用于监听客户端连接
    int m_CONNTrigmode;//connfd的触发模式，connfd用于与客户端消息收发

    //定时器相关
    client_data *users_timer;
    Utils utils;
};

#endif


