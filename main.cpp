#include <iostream>
#include <cstring>
#include "DistributedMonitor.h"
using namespace std;

class ProducerConsumer : public DistributedMonitor{
public:
    int buf =0;
    int flag = 0;
    ProducerConsumer() : DistributedMonitor() {}
    void serializeData() override{
        dataSize = sizeof(buf)+sizeof(flag);
        data = new char[dataSize];
        memcpy(data,&buf,sizeof(buf));
        memcpy(data+sizeof(buf),&flag,sizeof(flag));
    }

    void deserializeData() override{
        memcpy(&buf, data, sizeof(buf));
        memcpy(&flag, data+sizeof(buf), sizeof(flag));
    }

    void producer(){
        for(int i=0;i<20;i++) {
            lock();
            while (flag != 0) wait(1);
            buf=i;
            flag=1;
            notifyAll(1);
            unlock();
        }
    }

    void consumer(){
        for(int i=0;i<20;i++) {
            lock();
            while (flag != 1) wait(1);
            cout<<"BUFOR "<<buf<<endl;
            flag=0;
            notifyAll(1);
            unlock();
        }
    }

};

int main(int argc, char* argv[]) {

    ProducerConsumer *monitor = new ProducerConsumer();

    if (monitor->rank <1){
        monitor->producer();
    }else{
        monitor->consumer();
    }

    std::getchar();

}