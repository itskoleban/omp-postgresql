#include "pg/engine.hpp"
#include <iostream>
namespace pg {
Lane::Lane(std::string service, Limits limits, std::filesystem::path root,
           CompletionQueue &completed, Diagnostics *diagnostics, Owner owner, Id connection, int lane)
 : service_(std::move(service)), limits_(limits), sql_root_(std::move(root)), completed_(completed),
   diagnostics_(diagnostics), owner_(owner), connection_(connection), lane_(lane) {
 state.store(State::connected);
}
Lane::~Lane() { close(); }
void Lane::close() { state.store(State::disconnected); stopped.store(true); }
void Lane::enqueue(std::shared_ptr<Job>) {}
struct EngineTestAccess {
 static void seed(Engine& e, Id next) { e.next_=next; }
 static Id next(const Engine& e) { return e.next_; }
 static void confirm(Engine& e, const std::shared_ptr<Job>& j) {
  j->status.store(Status::confirmed); e.completed_.push(j);
 }
};
}
int main() {
 constexpr pg::Owner owner=510;
 constexpr auto maximum=std::numeric_limits<pg::Id>::max();
 pg::Engine e;
 auto c=e.connect(owner,"identity_model",1);
 pg::EngineTestAccess::seed(e,maximum-1);
 auto begin=e.begin(owner,c,0,0,true);
 pg::EngineTestAccess::confirm(e,e.job(owner,begin));
 e.poll();
 bool blocked=false;
 try { e.finish(owner,begin,true,0,true); }
 catch(const pg::Failure& f) { blocked=f.code==pg::Error::limit; }
 if(begin!=maximum-1 || !blocked || pg::EngineTestAccess::next(e)!=maximum) return 1;
 std::cout<<"BASELINE DEFECT REPRODUCED begin="<<begin<<" next="<<maximum
          <<" finish=Error::limit job_capacity=available SQL=not_run Lane=model\n";
}
