#include "server/model/groupmodel.h"
#include "db.h"


using namespace std;

vector<Group> GroupModel::query(int userid) {
    char sql[1024] = {0};
    sprintf(sql,
            "select a.id, a.groupname, a.groupdesc from AllGroup a inner join GroupUser b on a.id = b.groupid where b.userid = %d",
            userid);
    vector<Group> groupVec;
    MySQL mysql;
    if (mysql.connect()) {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr) {
            MYSQL_ROW row;
            // 查询userid所有的群组消息
            while ((row = mysql_fetch_row(res)) != nullptr) {
                Group group;
                group.setId(atoi(row[0]));
                group.setName(row[1]);
                group.setDesc(row[2]);
                groupVec.push_back(group);
            }
            mysql_free_result(res);
        }
    }
    // 查询群组的用户消息
    for (Group &group: groupVec) {
        sprintf(sql,
                "select a.id, a.name, a.state, b.grouprole from User a inner join GroupUser b on b.userid = a.id where b.groupid = %d",
                group.getId());
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)) != nullptr) {
                GroupUser user;
                user.setId(atoi(row[0]));
                user.setName(row[1]);
                user.setState(row[2]);
                user.setState(row[3]);
                group.getUsers().push_back(user);
            }
            mysql_free_result(res);
        }
    }
    return groupVec;
}

void GroupModel::addGroup(int userid, int groupid, std::string role) {
    char sql[1024] = {0};
    sprintf(sql, "insert into GroupUser values(%d, %d, '%s')", groupid, userid, role.c_str());
    MySQL mysql;
    if (mysql.connect()) {
        mysql.update(sql);
    }
}

bool GroupModel::creatGroup(Group &group) {
    char sql[1024] = {0};
    sprintf(sql, "insert into AllGroup(groupname, groupdesc) values('%s', '%s')", group.getName().c_str(),
            group.getDesc().c_str());
    MySQL mysql;
    if (mysql.connect()) {
        if (mysql.update(sql)) {
            group.setId(mysql_insert_id(mysql.getConnection()));
            return true;
        }
    }
    return false;
}

vector<int> GroupModel::queryGroupUsers(int userid, int groupid) {
    char sql[1024] = {0};
    sprintf(sql, "select userid from GroupUser where groupid = %d and userid != %d ", groupid, userid);
    vector<int> idVec;
    MySQL mysql;
    if (mysql.connect()) {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)) != nullptr) {
                idVec.push_back(atoi(row[0]));
            }
            mysql_free_result(res);
        }
    }
    return idVec;
}





















