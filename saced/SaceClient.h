#ifndef _SACE_CLIENT_H_
#define _SACE_CLIENT_H_

#include <stdio.h>
#include <stdlib.h>

struct SaceClientIdentifier {
    uid_t uid;
    pid_t pid;

    SaceClientIdentifier ():SaceClientIdentifier(-1, -1) {}
    SaceClientIdentifier (uid_t uid, pid_t pid):uid(uid),pid(pid) {}

    SaceClientIdentifier (const SaceClientIdentifier& client) {
        uid = client.uid;
        pid = client.pid;
    }

    SaceClientIdentifier& operator= (const SaceClientIdentifier& client) {
        uid = client.uid;
        pid = client.pid;
        return *this;
    }

    bool operator> (const SaceClientIdentifier& client) const {
        return uid > client.uid || (uid == client.uid && pid > client.pid);
    }

    bool operator< (const SaceClientIdentifier& client) const {
        return uid < client.uid || (uid == client.uid && pid < client.pid);
    }

    bool operator== (const SaceClientIdentifier& client) const {
        return uid == client.uid && pid == client.pid;
    }
};

#endif