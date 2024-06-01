#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_connection_pool.h"

using namespace std;

connection_pool::connection_pool()
{
	m_CurConn = 0;		// 当前连接数
	m_FreeConn = 0;		// 空闲连接数
}

connection_pool *connection_pool::GetInstance()	// 获取数据库连接池实列的单例模式，使用静态局部变量确保全局唯一的实例
{
	static connection_pool connPool;
	return &connPool;
}

//构造初始化
void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, int MaxConn, int close_log)
{
	m_url = url;
	m_Port = Port;
	m_User = User;
	m_PassWord = PassWord;
	m_DatabaseName = DBName;
	m_close_log = close_log;

	for (int i = 0; i < MaxConn; i++)	// 循环创建 Maxconn 个MySQL连接，初始化失败则记录错误并退出
	{
		MYSQL *con = NULL;			
		con = mysql_init(con);

		if (con == NULL)
		{
			LOG_ERROR("MySQL Error");
			exit(1);
		}
		con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);

		if (con == NULL)
		{
			LOG_ERROR("MySQL Error");
			exit(1);
		}
		connList.push_back(con);	// 创建连接后，将连接添加到connList列表中
		++m_FreeConn;				// 空闲连接数+1
	}

	reserve = sem(m_FreeConn);  // 初始化信号量 reserve , 值为空闲连接数

	m_MaxConn = m_FreeConn;		// 设置最大连接数
}


//当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection()
{
	MYSQL *con = NULL;

	if (0 == connList.size())
		return NULL;

	reserve.wait();			// 使用信号量等待可用连接
	
	lock.lock();			// 加锁保护，获取连接并更新连接池状态	加锁有什么用？

	con = connList.front();		// 可用连接为 connList的头指针
	connList.pop_front();		

	--m_FreeConn;		// 空闲连接--
	++m_CurConn;

	lock.unlock();		
	return con;
}

//释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL *con)
{
	if (NULL == con)
		return false;

	lock.lock();

	connList.push_back(con);	// 将连接放回连接池，并更新连接池状态
	++m_FreeConn;
	--m_CurConn;

	lock.unlock();

	reserve.post();		// 增加信号量
	return true;
}

//销毁数据库连接池
void connection_pool::DestroyPool()
{

	lock.lock();
	if (connList.size() > 0)	// 关闭所有连接并清空连接池
	{
		list<MYSQL *>::iterator it;
		for (it = connList.begin(); it != connList.end(); ++it)
		{
			MYSQL *con = *it;
			mysql_close(con);
		}
		m_CurConn = 0;
		m_FreeConn = 0;
		connList.clear();
	}

	lock.unlock();
}

//当前空闲的连接数
int connection_pool::GetFreeConn()
{
	return this->m_FreeConn;
}

connection_pool::~connection_pool()
{
	DestroyPool();
}

// 构造函数，获取一个数据库连接并管理其生命周期。
connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool){
	*SQL = connPool->GetConnection(); 		// 从连接池获取一个连接
	
	conRAII = *SQL;			// 保存连接和连接池指针
	poolRAII = connPool;
}

connectionRAII::~connectionRAII(){
	poolRAII->ReleaseConnection(conRAII);		// 
}