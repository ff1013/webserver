#include<mysql/mysql.h>
#include<stdio.h>
#include<string>
#include<string.h>
#include<stdlib.h>
#include<list>
#include<pthread.h>
#include<iostream>
#include"../CGImysql/sql_connection_pool.h"

using namespace std;

connection_pool::connection_pool()
{
	m_CurConn = 0;//初始化现有连接数
	m_FreeConn = 0;//初始化空闲连接数
}

//单例懒汉模式创建连接池，c++11保证静态变量的线程安全性
connection_pool *connection_pool::GetInstance()
{
	static connection_pool connPool;
	return &connPool;
}

//构造初始化
void connection_pool::init(string url,string User,string PassWord,string DBName,int Port,int MaxConn,int close_log)
{
	//初始化数据库信息
	m_url=url;//主机地址
	m_Port=Port;//数据库端口号
	m_User=User;//用户名
	m_PassWord=PassWord;//密码
	m_DatabaseName=DBName;//数据库名
	m_close_log=close_log;//日志开关

	//创建MaxConn条数据库连接
	for(int i=0;i<MaxConn;i++){
		MYSQL *con=NULL;
		con=mysql_init(con);//创建单个连接
		if(con==NULL){//创建失败
			LOG_ERROR("MySQL Error");
			exit(-1);
		}
		con=mysql_real_connect(con,url.c_str(),User.c_str(),PassWord.c_str(),DBName.c_str(),Port,NULL,0);//连接数据库
		if(con==NULL){//连接失败
			LOG_ERROR("MySQL Error");
			exit(1);
		}
		//更新连接池和空闲连接数量（链表）
		connList.push_back(con);
		++m_FreeConn;
	}
	//将信号量初始化为最大连接次数
	reserve=sem(m_FreeConn);
	m_MaxConn=m_FreeConn;
}

//获取连接
//当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection(){
	MYSQL *con=NULL;
	if(connList.size()==0) return NULL;
	//取出连接，信号量原子减1，为0则等待
	reserve.wait();
	lock.lock();//上锁
	con=connList.front();//链表操作
	connList.pop_front();
	--m_FreeConn;//更新
	++m_CurConn;
	lock.unlock();//解锁
	return con;
}

//释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL *con)
{
	if(con==NULL) return false;//此连接不存在或已释放
	lock.lock();//上锁
	connList.push_back(con);//链表操作
	++m_FreeConn;//更新
	--m_CurConn;
	lock.unlock();//解锁
	//释放连接原子加1
	reserve.post();
	return true;
}

//销毁数据库连接池
void connection_pool::DestroyPool(){
	lock.lock();//上锁
	if(connList.size()>0){//连接池内仍存在连接
		//通过迭代器遍历，关闭数据库连接
		list<MYSQL *>::iterator it;
		for(it=connList.begin();it!=connList.end();++it){
			MYSQL *con=*it;
			mysql_close(con);//关闭单个连接
		}
		m_CurConn=0;//更新
		m_FreeConn=0;
		//清空List
		connList.clear();
	}
	lock.unlock();//解锁
}

//读取当前空闲的连接数
int connection_pool::GetFreeConn()
{
	return this->m_FreeConn;
}

//RAII机制来自动释放
connection_pool::~connection_pool(){
	DestroyPool();
}

//通过RAII机制进行获取
//不直接调用获取和释放连接的接口，将其封装起来
connectionRAII::connectionRAII(MYSQL **SQL,connection_pool *connPool){
	*SQL=connPool->GetConnection();//调用获取连接
	conRAII=*SQL;
	poolRAII=connPool;//调用创建的连接池
}

//通过RAII机制进行释放
connectionRAII::~connectionRAII(){
	poolRAII->ReleaseConnection(conRAII);
}