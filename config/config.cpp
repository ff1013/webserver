#include "config.h"

Config::Config()
{
    //端口号,默认9006
    PORT = 9006;

    //日志写入方式，默认同步
    LOGWrite = 0;

    //触发组合模式,默认listenfd LT + connfd LT
    TRIGMode = 0;

    //listenfd触发模式，默认LT
    LISTENTrigmode = 0;

    //connfd触发模式，默认LT
    CONNTrigmode = 0;

    //优雅关闭链接，默认不使用
    OPT_LINGER = 0;

    //数据库连接池数量,默认8
    sql_num = 8;

    //线程池内的线程数量,默认8
    thread_num = 8;

    //关闭日志,默认不关闭
    close_log = 0;

    //并发模型,默认是模拟proactor
    actor_model = 0;
}

void Config::parse_arg(int argc, char*argv[])
{
    int opt;
    const char *str = "p:l:m:o:s:t:c:a:h";
    while ((opt = getopt(argc, argv, str)) != -1)//有效时返回有效的opt字符
    {
        switch (opt)//匹配字符
        {
        case 'p':
        {
            PORT = atoi(optarg);//opt指向的是选项参数的char*，转成int
            cout<<opt<<endl;
            break;
        }
        case 'l':
        {
            LOGWrite = atoi(optarg);
            break;
        }
        case 'm':
        {
            TRIGMode = atoi(optarg);
            break;
        }
        case 'o':
        {
            OPT_LINGER = atoi(optarg);
            break;
        }
        case 's':
        {
            sql_num = atoi(optarg);
            break;
        }
        case 't':
        {
            thread_num = atoi(optarg);
            break;
        }
        case 'c':
        {
            close_log = atoi(optarg);
            break;
        }
        case 'a':
        {
            actor_model = atoi(optarg);
            break;
        }
        case 'h':
        {
            //p 端口号 l日志写入方式 m触发模式 o优雅关闭 s连接池数据库连接数量 t线程池中线程数 c日志开关 a并发模型 
            printf("-p port\n");
            printf("-l how to write log,0->synchronous,1->asynchronous\n");
            printf("-m TRIGMode:0~3,four modes can be chosen\n");
            printf("-o OPT_LINGER open or not\n");
            printf("-s sql_num\n");
            printf("-t thread_num\n");
            printf("-c log open or not,0->open,1->not open\n");
            printf("-a actor_model,0->imitate proactor,1->reactor\n");
            printf("-h help\n");
            exit(0);
            break;
        }
        default://未知参数，报错并退出
            printf("parameter is no valid. \n");
            printf("please input -h to view parameter help\n");
            exit(0);
            break;
        }
    }
}