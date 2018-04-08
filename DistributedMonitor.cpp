//
// Created by jan on 18.03.18.
//

#include "DistributedMonitor.h"
#include <mpi.h>
#include <iostream>
#include <thread>
#include <mutex>
#include <cstring>

DistributedMonitor::DistributedMonitor() {
    MPI_Init(NULL, NULL);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    RN = std::vector<int>(world_size);
    initializeTokenIn0Node();
    for (int i = 0; i < world_size; i++) {
        RN[i] = 0;
    }
    runReceivingThread();
}

void DistributedMonitor::initializeTokenIn0Node() {
    if (rank == 0) {
        token = new Token();
        token->queueSize = 0;
        token->queue = new int[0];
        token->LN = new int[world_size];
        allowedToEnterCS = true;
    }
}

void DistributedMonitor::lock() {
    std::unique_lock<std::mutex> lock(mutex);
    enterCS(lock);
}

void DistributedMonitor::unlock() {
    std::unique_lock<std::mutex> lock(mutex);
    exitCS();
}

void DistributedMonitor::wait(int id){
    std::unique_lock<std::mutex> lock(mutex);
    broadcastWait(id);
    exitCS();
    waitAt = id;
    conditionalNotified = false;
    conditionalVariable.wait(lock, [this]() -> bool {
        return conditionalNotified;
    });
    enterCS(lock);
}

void DistributedMonitor::notifyAll(int id){
    std::unique_lock<std::mutex> lock(mutex);
    if(awaitings.count(id)){
        auto vector = awaitings.at(id);
        for(int i = 0;i<vector.size();i++){
            MPI_Send(&id, 1, MPI_INT, vector.back(), MessageType::NOTIFY, MPI_COMM_WORLD);
            vector.pop_back();
        }
        awaitings.erase(id);
    }
    sendClearAwaitings(id);
}

void DistributedMonitor::enterCS(unique_lock<std::mutex> &lock) {
    waitForToken = true;
    sendCSRequest();

    if (token == nullptr) {
        conditionalVariable.wait(lock, [this]() -> bool {
            return allowedToEnterCS;
        });
    }

    isInCS = true;
    waitForToken = false;

}

void DistributedMonitor::exitCS() {
    sendData();
    token->LN[rank] = RN[rank];
    for (int i = 0; i < world_size; i++) {
        if (RN[i] == token->LN[i] + 1) {
            bool isInQueue = false;
            for (int j = 0; j < token->queueSize; j++) {
                if (token->queue[j] == i) isInQueue = true;
            }
            if (!isInQueue) {
                int newSize = token->queueSize + 1;
                int *newQueue = new int[newSize];
                memcpy(newQueue, token->queue, token->queueSize * sizeof(int));
                delete[] token->queue;
                newQueue[newSize - 1] = i;
                token->queue = newQueue;
                token->queueSize = newSize;
            }
        }
    }
    if (token->queueSize > 0) {
        int newSize = token->queueSize - 1;
        int *newQueue = new int[newSize];

        int firstInQueue = token->queue[0];
        if (newSize > 0) {
            memcpy(newQueue, &(token->queue)[1], newSize * sizeof(int));
        }
        delete[] token->queue;
        token->queue = newQueue;
        token->queueSize = newSize;
        sendToken(firstInQueue);
    }

    isInCS = false;

}

void DistributedMonitor::onToken() {
    allowedToEnterCS = true;
    conditionalVariable.notify_one();
}

void DistributedMonitor::sendToken(int destination) {
    MPI_Send(&token->queueSize, 1, MPI_INT, destination, MessageType::TOKEN_QUEUE_SIZE, MPI_COMM_WORLD);
    MPI_Send(token->LN, world_size, MPI_INT, destination, MessageType::TOKEN_LN, MPI_COMM_WORLD);
    MPI_Send(token->queue, token->queueSize, MPI_INT, destination, MessageType::TOKEN_QUEUE, MPI_COMM_WORLD);
    allowedToEnterCS = false;
    delete[] token->LN;
    delete token;
    token = nullptr;
}

void DistributedMonitor::runReceivingThread() {
    std::thread communicatorThread(&DistributedMonitor::receive, this);
    communicatorThread.detach();
}

void DistributedMonitor::receive() {
    while (true) {

        MPI_Status status;
        MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

        std::unique_lock<std::mutex> lock(mutex);
        if (status.MPI_TAG == MessageType::TOKEN_QUEUE_SIZE ) {
            token = new Token();
            token->LN = new int(world_size);
            MPI_Recv(&token->queueSize, 1, MPI_INT, MPI_ANY_SOURCE, MessageType::TOKEN_QUEUE_SIZE, MPI_COMM_WORLD,
                     MPI_STATUS_IGNORE);
            token->queue = new int(token->queueSize);
            MPI_Recv(token->LN, world_size, MPI_INT, MPI_ANY_SOURCE, MessageType::TOKEN_LN, MPI_COMM_WORLD,
                     MPI_STATUS_IGNORE);
            MPI_Recv(token->queue, token->queueSize, MPI_INT, MPI_ANY_SOURCE, MessageType::TOKEN_QUEUE,
                     MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            // printToken();
            if (waitForToken) onToken();
        } else if (status.MPI_TAG == MessageType::DATA) {
            MPI_Get_count(&status, MPI_BYTE, &dataSize);
            data = new char[dataSize];
            MPI_Recv(data, dataSize, MPI_BYTE, MPI_ANY_SOURCE, MessageType::DATA, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            deserializeData();
        } else if (status.MPI_TAG == MessageType::ASK_FOR_TOKEN) {
            int sender = status.MPI_SOURCE;
            int sequenceNumber;
            MPI_Recv(&sequenceNumber, 1, MPI_INT, sender, MessageType::ASK_FOR_TOKEN, MPI_COMM_WORLD,
                     MPI_STATUS_IGNORE);
            if (sequenceNumber > RN[sender]) RN[sender] = sequenceNumber;
            if (!waitForToken&&token != nullptr && !isInCS && token->LN[sender] + 1 == RN[sender]) {
                sendToken(sender);
            }
        }else if(status.MPI_TAG == MessageType::WAIT){
            int conditionalID;
            int sender = status.MPI_SOURCE;
            MPI_Recv(&conditionalID, 1, MPI_INT, sender, MessageType::WAIT, MPI_COMM_WORLD,
                     MPI_STATUS_IGNORE);
            if(!awaitings.count(conditionalID)){
                awaitings.insert(pair<int,vector<int> >(conditionalID, vector<int>()));
            }
            awaitings.at(conditionalID).push_back(sender);
        }else if(status.MPI_TAG == MessageType::NOTIFY){
            int conditionalID;
            MPI_Recv(&conditionalID, 1, MPI_INT, MPI_ANY_SOURCE, MessageType::NOTIFY, MPI_COMM_WORLD,
                     MPI_STATUS_IGNORE);
            if(!conditionalNotified&&waitAt==conditionalID) {
                conditionalNotified = true;
                conditionalVariable.notify_one();
            }
        }else if(status.MPI_TAG == MessageType::CLEAR_AWAITINGS){
            int conditionalID;
            MPI_Recv(&conditionalID, 1, MPI_INT, MPI_ANY_SOURCE, MessageType::CLEAR_AWAITINGS, MPI_COMM_WORLD,
                     MPI_STATUS_IGNORE);
            if(awaitings.count(conditionalID)) awaitings.erase(conditionalID);
        }
    }
}

void DistributedMonitor::broadcastWait(int id) {
    for (int i = 0; i < world_size; i++) {
        if (i != rank) {
            MPI_Send(&id, 1, MPI_INT, i, WAIT, MPI_COMM_WORLD);
        }
    }
}

void DistributedMonitor::sendData() {
    serializeData();
    for (int i = 0; i < world_size; i++) {
        if (i != rank) {
            MPI_Send(data, dataSize, MPI_BYTE, i, MessageType::DATA, MPI_COMM_WORLD);
        }
    }
    delete[] data;
}

void DistributedMonitor::sendCSRequest() {
    RN[rank] += 1;
    for (int i = 0; i < world_size; i++) {
        if (i != rank) {
            MPI_Send(&RN[rank], 1, MPI_INT, i, MessageType::ASK_FOR_TOKEN, MPI_COMM_WORLD);
        }
    }
}

void DistributedMonitor::sendClearAwaitings(int id) {
    for (int i = 0; i < world_size; i++) {
        if (i != rank) {
            MPI_Send(&id, 1, MPI_INT, i, CLEAR_AWAITINGS, MPI_COMM_WORLD);
        }
    }
}

void DistributedMonitor::printToken() {
    cout << "RANK: " << rank << " LN: ";
    for (int i = 0; i < world_size; i++) {
        std::cout << token->LN[i] << " ";
    }
    cout << "QUEUE: ";
    for (int i = 0; i < token->queueSize; i++) {
        std::cout << token->queue[i]<<" ";
    }
    cout << endl<< std::flush;
}

void DistributedMonitor::serializeData() {}

void DistributedMonitor::deserializeData() {}


