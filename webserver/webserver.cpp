#include "webserver.h"

WebServer::WebServer()
{
    //http_conn类对象
    users = new http_conn[MAX_FD];

    //resources文件夹路径
    char server_path[200];
    getcwd(server_path, 200);
    char root[11] = "/resources";//请求的资源们所在的文件夹
    //把路径拼接起来
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);

    //定时器
    users_timer = new client_data[MAX_FD];
}

//服务器资源释放
WebServer::~WebServer()
{
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete m_pool;
}

//各种初始化
void WebServer::init(int port, string user, string passWord, string databaseName, int log_write, 
                     int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model)
{
    m_port = port;
    m_user = user;
    m_passWord = passWord;
    m_databaseName = databaseName;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_log_write = log_write;
    m_OPT_LINGER = opt_linger;
    m_TRIGMode = trigmode;
    m_close_log = close_log;
    m_actormodel = actor_model;
}

//设置epoll的触发模式，epoll有两种触发方式：ET、LT
void WebServer::trig_mode()
{
    //listenfd、connfd两两组合，所以epoll有四种模式
    //LT + LT
    if (0 == m_TRIGMode)//模式0
    {
        m_LISTENTrigmode = 0;//listenfd电平触发LT
        m_CONNTrigmode = 0;//connfd电平触发LT
    }
    //LT + ET
    else if (1 == m_TRIGMode)//模式1
    {
        m_LISTENTrigmode = 0;//listenfd电平触发LT
        m_CONNTrigmode = 1;//connfd电平触发LT
    }
    //ET + LT
    else if (2 == m_TRIGMode)//模式2
    {
        m_LISTENTrigmode = 1;//listenfd边缘触发ET
        m_CONNTrigmode = 0;//connfd边缘触发ET
    }
    //ET + ET
    else if (3 == m_TRIGMode)//模式3
    {
        m_LISTENTrigmode = 1;//listenfd边缘触发ET
        m_CONNTrigmode = 1;//connfd边缘触发ET
    }
}

//初始化日志
void WebServer::log_write()
{
    if (0 == m_close_log)
    {
        //初始化日志
        if (1 == m_log_write)//异步
            Log::get_instance()->init("./log_file/ServerLog", m_close_log, 2000, 800000, 800);
            //如果设置了max_queue_size,则为异步
        else//同步
            Log::get_instance()->init("./log_file/ServerLog", m_close_log, 2000, 800000, 0);
    }
}
 
//初始化数据库连接池
void WebServer::sql_pool()
{
    //初始化数据库连接池
    m_connPool = connection_pool::GetInstance();
    m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log);

    //初始化数据库读取表
    users->initmysql_result(m_connPool);
}

//创建线程池
void WebServer::thread_pool()
{
    //线程池
    m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

//监听，监听套接字listenfd、管道套接字pipefd相关设置
void WebServer::eventListen()
{
    //网络编程基础步骤
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    //如果它的条件返回错误，则终止程序执行
    assert(m_listenfd >= 0);

    //优雅关闭连接
    if (0 == m_OPT_LINGER)
    {
        //将未发送完的数据发送完成后再释放资源，优雅关闭
        struct linger tmp = {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if (1 == m_OPT_LINGER)
    {
        //超时时间到达前，发送完成并得到确认，优雅关闭
        //超时后closesocket直接返回
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;
     // 创建监听Socket的TCP/IP的IPv4 Socket地址
    struct sockaddr_in address;
    //bzero()清零内存块
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);// INADDR_ANY：将套接字绑定到所有可用的接口
    address.sin_port = htons(m_port);

    //设置端口复用
    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    //一般来说，一个端口释放后会等待两分钟之后才能再被使用，SO_REUSEADDR是让端口释放后立即就可以被再次使用
    //传统绑定步骤
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    //传统监听步骤
    ret = listen(m_listenfd, 5);
    assert(ret >= 0);

    //设置超时时间
    utils.init(TIMESLOT);

    //epoll创建内核事件表
    epoll_event events[MAX_EVENT_NUMBER];
    //创建一个额外的文件描述符来唯一标识内核中的EPOLL事件表
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

     // 将listenfd在内核事件表注册读事件
    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);
    // 初始化HTTP类对象的m_epollfd属性
    http_conn::m_epollfd = m_epollfd;

    //创建管道套接字
    //管道用于timer信号通知，管道写端用于写入信号，管道读端I/O复用用于系统检测是否有信号可读
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);

    //管道写端设置为非阻塞
    //send是将信息发送给套接字缓冲区，如果缓冲区满了则会阻塞，会进一步增加信号处理函数的执行时间
    utils.setnonblocking(m_pipefd[1]);
    
    //管道读端在内核事件表注册读事件，ET非阻塞
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);

    // 解决"对端关闭"问题？？？
    utils.addsig(SIGPIPE, SIG_IGN);

    //传递给主循环的信号值，这里只关注SIGALRM和SIGTERM
    // 设置捕捉SIGALRM定时器超时信号
    utils.addsig(SIGALRM, utils.sig_handler, false);
    // 设置捕捉SIGTERM程序结束信号（kill命令或Ctrl+C）
    utils.addsig(SIGTERM, utils.sig_handler, false);

    //循环条件
    //每隔TIMESLOT时间触发SIGALRM超时信号
    alarm(TIMESLOT);

    //初始化工具类,信号和描述符基础操作
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}

//运行，处理套接字事件
void WebServer::eventLoop()
{
    //超时标志
    bool timeout = false;
    //循环条件
    bool stop_server = false;

    while (!stop_server)
    {
        //监测发生事件的文件描述符
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        //轮询文件描述符
        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;

            //处理新到的客户连接
            if (sockfd == m_listenfd)
            {
                bool flag = dealclinetdata();
                if (false == flag)
                    continue;
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                //服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            //管道读端对应文件描述符发生读事件
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                bool flag = dealwithsignal(timeout, stop_server);
                if (false == flag)
                    LOG_ERROR("%s", "dealclientdata failure");
            }
            //处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN)
            {
                dealwithread(sockfd);
            }
            //处理写
            else if (events[i].events & EPOLLOUT)
            {
                dealwithwrite(sockfd);
            }
        }
        //处理定时器为非必须事件，收到信号并不是立马处理
        //完成读写事件后，再进行处理
        if (timeout)
        {
            utils.timer_handler();

            LOG_INFO("%s", "timer tick");

            timeout = false;
        }
    }
}

//初始化定时器
void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_passWord, m_databaseName);

    //初始化client_data数据
    //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中

    //初始化该连接对应的连接资源
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;

    //创建定时器临时变量
    util_timer *timer = new util_timer;
    //设置定时器对应的连接资源
    timer->user_data = &users_timer[connfd];
    //设置回调函数
    timer->cb_func = cb_func;

    time_t cur = time(NULL);
    //设置绝对超时时间
    timer->expire = cur + 3 * TIMESLOT;
    //创建该连接对应的定时器，初始化为前述临时变量
    users_timer[connfd].timer = timer;
    //将该定时器添加到链表中
    utils.m_timer_lst.add_timer(timer);
}

//若有数据传输，则将定时器往后延迟3个单位
//并对新的定时器在链表上的位置进行调整
void WebServer::adjust_timer(util_timer *timer)
{
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);

    LOG_INFO("%s", "adjust timer once");
}

//关闭连接，移除定时器
void WebServer::deal_timer(util_timer *timer, int sockfd)
{
    //服务器端关闭连接，移除对应的定时器
    timer->cb_func(&users_timer[sockfd]);
    if (timer)
    {
        utils.m_timer_lst.del_timer(timer);
    }

    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

//
bool WebServer::dealclinetdata()
{
    //初始化客户端连接地址
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);

    if (0 == m_LISTENTrigmode)
    {
        //该连接分配的文件描述符
        int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
        if (connfd < 0)
        {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if (http_conn::m_user_count >= MAX_FD)
        {
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        timer(connfd, client_address);
    }

    else
    {
        while (1)
        {
            //该连接分配的文件描述符
            int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
            if (connfd < 0)
            {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if (http_conn::m_user_count >= MAX_FD)
            {
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            timer(connfd, client_address);
        }
        return false;
    }
    return true;
}

//处理信号，从管道读端读出信号值
bool WebServer::dealwithsignal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    int sig;
    char signals[1024];

    //从管道读端读出信号值，成功返回字节数，失败返回-1
    //正常情况下，这里的ret返回值总是1，只有14、15两个ASCII码对应的字符
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1)
    {
        return false;
    }
    else if (ret == 0)
    {
        return false;
    }
    else
    {
        //处理信号值对应的逻辑
        for (int i = 0; i < ret; ++i)
        {
            //switch里面是字符
            switch (signals[i])
            {
            //case里面是整型，因为可以为字符对应ASCII码
            case SIGALRM:
            {
                timeout = true;//超时
                break;
            }
            case SIGTERM:
            {
                stop_server = true;//程序结束
                break;
            }
            }
        }
    }
    return true;
}
//处理客户连接上接收到的数据
void WebServer::dealwithread(int sockfd)
{
    //创建定时器临时变量，将该连接对应的定时器取出来
    util_timer *timer = users_timer[sockfd].timer;

    //reactor
    if (1 == m_actormodel)
    {
        if (timer)
        {
            //有数据传输，调整定时器
            adjust_timer(timer);
        }

        //若监测到读事件，将该事件放入请求队列
        m_pool->append(users + sockfd, 0);

        while (true)
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        //proactor
        if (users[sockfd].read_once())
        {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            //若监测到读事件，将该事件放入请求队列
            m_pool->append_p(users + sockfd);

            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}

void WebServer::dealwithwrite(int sockfd)
{
    //创建定时器临时变量，将该连接对应的定时器取出来
    util_timer *timer = users_timer[sockfd].timer;
    //reactor
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer);
        }

        m_pool->append(users + sockfd, 1);

        while (true)
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        //proactor
        if (users[sockfd].write())
        {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}


