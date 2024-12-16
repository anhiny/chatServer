#ifndef CHAT_GROUPMODEL_H
#define CHAT_GROUPMODEL_H

#include <vector>
#include "group.h"

using namespace std;

class GroupModel {
public:
    // 创建群组
    bool creatGroup(Group &group);

    // 加入群组
    void addGroup(int userid, int groupid, string role);

    // 查询用户所在群组信息
    vector<Group> query(int userid);

    // 根据指定的groupId查询群组用户id列表，除了用户id自己，主要用于群发消息
    vector<int> queryGroupUsers(int userid, int groupid);
};


#endif //CHAT_GROUPMODEL_H
