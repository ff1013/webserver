#ifndef LST_TIMER
#define LST_TIMER

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <time.h>

#include "../log/log.h"

//连接资源结构体成员需要用到定时器类
//需要前向声明
class util_timer;

//连接资源类
struct client_data
{
    //客户端socket地址
    sockaddr_in address;
    //socket文件描述符
    int sockfd;
    //定时器
    util_timer *timer;
};

//定时器类
class util_timer
{
public:
    util_timer() : prev(NULL), next(NULL) {}//初始化前向定时器、后继定时器

public:
    //超时时间
    time_t expire;
    //回调函数,释放连接资源
    void (* cb_func)(client_data *);
    //连接资源
    client_data *user_data;
    //前向定时器
    util_timer *prev;
    //后继定时器
    util_timer *next;
};

//定时器容器类【双向链表】
class sort_timer_lst
{
public:
    sort_timer_lst();
    //常规销毁链表
    ~sort_timer_lst();

    //添加定时器到链表中
    void add_timer(util_timer *timer);
    //调整定时器位置
    void adjust_timer(util_timer *timer);
    //删除超时定时器
    void del_timer(util_timer *timer);
    //定时任务处理函数
    void tick();

private:
    void add_timer(util_timer *timer, util_timer *lst_head);

    util_timer *head;//头节点
    util_timer *tail;//尾节点
};

//工具类
class Utils
{
public:
    Utils() {}
    ~Utils() {}

    void init(int timeslot);

    //对文件描述符设置非阻塞
    int setnonblocking(int fd);

    //将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    //信号处理函数
    //仅仅通过管道发送信号值，不处理信号对应的逻辑，缩短异步执行时间，减少对主程序的影响
    static void sig_handler(int sig);

    //设置信号函数
    void addsig(int sig, void(handler)(int), bool restart = true);

    //定时处理任务，重新定时以不断触发SIGALRM信号
    void timer_handler();

    void show_error(int connfd, const char *info);

public:
    static int *u_pipefd;//管道
    sort_timer_lst m_timer_lst;
    static int u_epollfd;
    int m_TIMESLOT;
};

//定时器回调函数【在定时器类中使用】
void cb_func(client_data *user_data);

#endif