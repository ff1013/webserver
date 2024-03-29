#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include<stdio.h>
#include<list>
#include<mysql/mysql.h>
#include<error.h>
#include<string.h>
#include<iostream>
#include<string>
#include"../lock/locker.h"
#include"../log/log.h"

using namespace std;

class connection_pool
{
public:
    MYSQL *GetConnection();//获取数据库连接
    bool ReleaseConnection(MYSQL *conn);//释放连接
    int GetFreeConn();//获取连接
    void DestroyPool();//销毁所有连接

    //局部静态变量单例模式
    static connection_pool *GetInstance();

    void init(string url,string User,string PassWord,string DataBaseName,int Port,int MaxConn,int close_log);
private:
    connection_pool();
    ~connection_pool();

    int m_MaxConn;//最大连接数
    int m_CurConn;//当前已使用的连接数
    int m_FreeConn;//当前空闲的连接数
    locker lock;
    list<MYSQL *> connList;//连接池
    sem reserve;//信号量
public:
    string m_url;//主机地址
    string m_Port;//数据库端口号
    string m_User;//登陆数据库用户名
    string m_PassWord;//登陆数据库密码
    string m_DatabaseName;//使用数据库名
    int m_close_log;//日志开关
};

class connectionRAII
{
public:
    //双指针对MYSQL *con修改
    //在获取连接时，通过有参构造对传入的参数进行修改。
    //其中数据库连接本身是指针类型，所以参数需要通过双指针才能对其进行修改。
    connectionRAII(MYSQL **con,connection_pool *connPool);
    ~connectionRAII();
private:
    MYSQL *conRAII;
    connection_pool *poolRAII;
};


#endif