//
// Created by jan on 18.03.18.
//

#ifndef DISTRIBUTEDMONITOR_COMMUNICATOR_H
#define DISTRIBUTEDMONITOR_COMMUNICATOR_H


#include <map>
#include <string>
#include <condition_variable>
#include <vector>

using namespace std;

enum MessageType {
    TOKEN_QUEUE_SIZE,TOKEN_QUEUE,TOKEN_LN, ASK_FOR_TOKEN,DATA,WAIT, NOTIFY,CLEAR_AWAITINGS
};

struct Token{
    int *queue;
    int *LN;
    int queueSize;

};

class DistributedMonitor {
public:

    char* data;
    int dataSize;
    int world_size;
    int rank;

    DistributedMonitor();
    void lock();
    void unlock();
    void wait(int id);
    void notifyAll(int id);
    virtual void serializeData();
    virtual void deserializeData();

private:
    bool isInCS = false;
    bool waitForToken = false;
    bool allowedToEnterCS = false;
    bool conditionalNotified;
    int waitAt;

    std::vector<int> RN;
    std::map<int,std::vector<int>> awaitings;
    Token* token = nullptr;

    condition_variable conditionalVariable;
    std::mutex mutex;

    void initializeTokenIn0Node();
    void enterCS(unique_lock<std::mutex> &lock);
    void exitCS();

    void receive();
    void onToken();
    void runReceivingThread();
    void sendToken(int destination);
    void sendData();

    void sendCSRequest();
    void printToken();
    void broadcastWait(int id);
    void sendClearAwaitings(int id);
};


#endif //DISTRIBUTEDMONITOR_COMMUNICATOR_H
