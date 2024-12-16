#ifndef CHAT_DB_H
#define CHAT_DB_H


#include <string>
#include <mysql/mysql.h>


using namespace std;




// 数据库操作类

class MySQL {
public:
    MySQL();

    ~MySQL();

    // 连接数据库
    bool connect();

    // 更新操作
    bool update(string sql);

    // 查询操作
    MYSQL_RES *query(string sql);

    // 获取连接
    MYSQL *getConnection();

private:
    MYSQL *_conn;
};


#endif //CHAT_DB_H
