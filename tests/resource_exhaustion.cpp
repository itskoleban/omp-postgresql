// Linux-only descriptor exhaustion, inside the disposable PostgreSQL fixture process.
#include "pg/engine.hpp"
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <sys/resource.h>
#include <thread>
#include <unistd.h>

namespace {
constexpr pg::Owner owner=909;
void check(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
template<class F> void until(pg::Engine &e,F f){
    auto deadline=pg::Clock::now()+std::chrono::seconds(10);
    while(!f()){
        e.poll();
        check(pg::Clock::now()<deadline,"descriptor test deadline");
        std::this_thread::yield();
    }
}
void query(pg::Engine &e,pg::Id c){
    auto j=e.job(owner,e.query(owner,c,"SELECT 42",0,0,0,false));
    until(e,[&]{return j->delivered;});
    check(j->status.load()==pg::Status::confirmed && j->completed==1 &&
          j->results.at(0).rows.at(0).at(0)=="42","existing/recovered transport failed");
    e.close_job(owner,j->id);
}
struct Descriptors {
    rlimit original{};
    std::vector<int> held;
    Descriptors(){
        held.reserve(128);
        check(getrlimit(RLIMIT_NOFILE,&original)==0,"getrlimit failed");
        auto limited=original;
        limited.rlim_cur=std::min<rlim_t>(original.rlim_cur,64);
        check(limited.rlim_cur>=16 && setrlimit(RLIMIT_NOFILE,&limited)==0,
              "cannot establish disposable descriptor limit");
    }
    void release(){
        for(auto fd:held)close(fd);
        held.clear();
        check(setrlimit(RLIMIT_NOFILE,&original)==0,"restore descriptor limit failed");
    }
    ~Descriptors(){
        for(auto fd:held)close(fd);
        setrlimit(RLIMIT_NOFILE,&original);
    }
};
}
int main(){
    auto service=std::getenv("OMP_PG_TEST_SERVICE");
    if(!service)return 77;
    try{
        pg::Limits limits;
        limits.reconnect_attempts=0;
        pg::Engine e(limits);
        auto existing=e.connect(owner,service,1);
        until(e,[&]{return e.connection(owner,existing).state()==pg::State::connected;});
        query(e,existing);
        Descriptors descriptors;
        for(;;){
            auto fd=open("/dev/null",O_RDONLY|O_CLOEXEC);
            if(fd<0){check(errno==EMFILE,"expected actual per-process EMFILE");break;}
            descriptors.held.push_back(fd);
        }
        check(!descriptors.held.empty(),"descriptor exhaustion was not exercised");
        auto refused=e.connect(owner,service,1);
        until(e,[&]{return e.connection(owner,refused).state()==pg::State::failed;});
        check(e.connection(owner,refused).lanes.at(0)->snapshot().sql_commands==0,
              "SQL dispatched on a failed connection");
        query(e,existing); // An already open TLS transport remains valid at EMFILE.
        e.disconnect(owner,existing);
        e.disconnect(owner,refused);
        auto deadline=pg::Clock::now()+std::chrono::seconds(10);
        while(!e.connection(owner,existing).lanes.at(0)->stopped.load() ||
              !e.connection(owner,refused).lanes.at(0)->stopped.load()){
            check(pg::Clock::now()<deadline,"shutdown stalled under EMFILE");
            std::this_thread::yield();
        }
        auto held=descriptors.held.size();
        descriptors.release();
        e.poll(); e.poll();
        auto recovered=e.connect(owner,service,1);
        until(e,[&]{return e.connection(owner,recovered).state()==pg::State::connected;});
        query(e,recovered);
        std::cout<<"resource_exhaustion PASS actual=EMFILE held="<<held
                 <<" existing_tls=confirmed failed_dispatches=0 shutdown=complete recovery=confirmed\n";
        return 0;
    }catch(const std::exception &error){
        std::cerr<<"resource_exhaustion FAIL "<<error.what()<<'\n';
        return 1;
    }
}
