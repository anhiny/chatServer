#include "server/model/offlinemessagemodel.h"
#include "db.h"
#include <muduo/base/Logging.h>
#include <string>

using namespace muduo;

void OfflineMsgModel::insert(int userid, std::string msg) {
    // 处理消息中的单引号，防止SQL注入
    string safe_msg = msg;
    size_t pos = 0;
    while ((pos = safe_msg.find('\'', pos)) != string::npos) {
        safe_msg.replace(pos, 1, "''");
        pos += 2;
    }

    // 组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "insert into offlineMessage values(%d, '%s')", userid, safe_msg.c_str());

    MySQL mysql;
    if (mysql.connect()) {
        if (!mysql.update(sql)) {
            LOG_ERROR << "Failed to insert offline message for user " << userid;
        }
    } else {
        LOG_ERROR << "Database connection failed when inserting offline message";
    }
}


vector<string> OfflineMsgModel::query(int userid) {
    // 组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "select message from offlineMessage where userid = %d", userid);
    vector<string> vec;
    MySQL mysql;
    if (mysql.connect()) {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr) {
            // 把userid用户的所有离线消息放入vec中返回
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)) != nullptr) {
                vec.push_back(row[0]);
            }

            mysql_free_result(res);
        } else {
            LOG_ERROR << "Failed to query offline messages for user " << userid;
        }
    } else {
        LOG_ERROR << "Database connection failed when querying offline messages";
    }
    return vec;
}

void OfflineMsgModel::remove(int userid) {
    // 组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "delete from offlineMessage where userid = %d", userid);

    MySQL mysql;
    if (mysql.connect()) {
        if (!mysql.update(sql)) {
            LOG_ERROR << "Failed to remove offline messages for user " << userid;
        }
    } else {
        LOG_ERROR << "Database connection failed when removing offline messages";
    }
}
