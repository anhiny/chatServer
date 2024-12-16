#include "server/model/offlinemessagemodel.h"
#include "db.h"


void OfflineMsgModel::insert(int userid, std::string msg) {
    // 组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "insert into offlineMessage values(%d, '%s')", userid, msg.c_str());

    MySQL mysql;
    if (mysql.connect()) {
        mysql.update(sql);
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
            return vec;
        }
    }
    return vec;
}

void OfflineMsgModel::remove(int userid) {
    // 组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "delete from offlineMessage where userid = %d", userid);

    MySQL mysql;
    if (mysql.connect()) {
        mysql.update(sql);
    }
}
