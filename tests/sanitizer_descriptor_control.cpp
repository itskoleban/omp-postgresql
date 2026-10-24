// Independent sanitizer control: no omp-postgresql headers or objects are linked.
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/resource.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include "sanitizer_descriptor_control.hpp"
int main() {
    auto object=create_descriptor_control();
    std::atomic<bool> finish{false};
    std::thread worker([&]{while(!finish.load())std::this_thread::yield();});
    rlimit original{};
    if(getrlimit(RLIMIT_NOFILE,&original))return 2;
    auto small=original; small.rlim_cur=64;
    if(setrlimit(RLIMIT_NOFILE,&small))return 3;
    std::vector<int> held; held.reserve(64);
    for(;;){
        int fd=open("/dev/null",O_RDONLY|O_CLOEXEC);
        if(fd<0){if(errno!=EMFILE)return 4;break;}
        held.push_back(fd);
    }
    std::fprintf(stderr,"CONTROL reached actual EMFILE; joining ordinary std::thread\n");
    // This valid object has not been dereferenced by the instrumented driver yet.
    // Unlike a thread trampoline, its dynamic-type check is guaranteed to be cold.
    std::fprintf(stderr,"CONTROL checking a valid external polymorphic object at EMFILE\n");
    if(object->value()!=42)return 5;
    finish.store(true);
    worker.join();
    for(auto fd:held)close(fd);
    setrlimit(RLIMIT_NOFILE,&original);
    destroy_descriptor_control(object);
    std::puts("CONTROL completed with no component code linked");
}
