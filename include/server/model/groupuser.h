#ifndef CHAT_GROUPUSER_H
#define CHAT_GROUPUSER_H

#include "user.h"


class GroupUser : public User {
public:
    void setRole(string role) { this->role = role; }

    string getRole() { return this->role; }

private:
    string role;
};


#endif //CHAT_GROUPUSER_H
