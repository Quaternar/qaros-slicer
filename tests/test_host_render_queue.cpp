#include "qarvtk/host_render_queue.hpp"
#include <atomic>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

using Queue=qarvtk::HostRenderQueue<int>;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class F> void rejected(F action){bool threw=false;try{action();}catch(const std::runtime_error&){threw=true;}require(threw,"Invalid operation accepted");}
void complete(Queue& queue,const Queue::Handle& job){
  for(unsigned eye=0;eye<2;++eye)for(bool depth:{false,true}){
    queue.check_plane(job->serial,eye,depth);queue.complete_plane(job->serial,eye,depth);
    rejected([&]{queue.check_plane(job->serial,eye,depth);});
  }
  queue.finish(job->serial);
}
int main(){try{
  Queue queue(2);queue.enable(0);queue.enable(1);
  auto a=queue.post(0,10),b=queue.post(1,20);
  rejected([&]{queue.post(0,11);});
  require(queue.acquire()==a,"FIFO order lost");
  rejected([&]{queue.acquire();});rejected([&]{queue.finish(a->serial);});
  complete(queue,a);
  // A has not even consumed its completed frame. B can already render.
  require(queue.acquire()==b,"B blocked behind A's SDK worker");complete(queue,b);
  require(queue.wait(a)&&queue.wait(b),"Completed frame lost");
  queue.consume(a);queue.consume(b);

  // Deterministically hold A inside a simulated SDK show_frame. B must finish
  // 100 cycles while A remains held, with no sleeps or scheduler assumptions.
  std::promise<void> showing_a,release_a;
  auto release=release_a.get_future();
  auto worker_a=std::async(std::launch::async,[&]{showing_a.set_value();release.wait();});
  showing_a.get_future().wait();
  for(int i=0;i<100;++i){
    auto next=queue.post(1,i);require(queue.acquire()==next,"B was not scheduled");
    complete(queue,next);require(queue.wait(next),"B did not finish");queue.consume(next);
  }
  release_a.set_value();worker_a.get();

  Queue concurrent(2);concurrent.enable(0);concurrent.enable(1);
  std::atomic<unsigned> producers_done{0};
  auto produce=[&](unsigned route){
    for(unsigned i=0;i<500;++i){
      auto job=concurrent.post(route,int(route));
      require(concurrent.wait(job),"Concurrent frame cancelled");concurrent.consume(job);
    }
    ++producers_done;
  };
  auto producer_a=std::async(std::launch::async,produce,0);
  auto producer_b=std::async(std::launch::async,produce,1);
  unsigned rendered=0;std::uint64_t previous=0;
  while(producers_done!=2){
    if(auto job=concurrent.acquire()){
      require(job->serial>previous&&job->payload==int(job->route),"Frame identity/order corrupted");
      previous=job->serial;complete(concurrent,job);++rendered;
    }else std::this_thread::yield();
  }
  producer_a.get();producer_b.get();require(rendered==1000,"Concurrent frames lost or duplicated");

  auto cancelled=queue.post(0,30);queue.disable(0);
  require(!queue.acquire()&&!queue.wait(cancelled),"Cancelled queued frame acquired");queue.consume(cancelled);
  queue.enable(0);
  auto acquired=queue.post(0,40);require(queue.acquire()==acquired,"Missing active frame");
  queue.disable(0);
  rejected([&]{queue.enable(0);});
  auto waiting=std::async(std::launch::async,[&]{return queue.wait(acquired);});
  require(waiting.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout,
          "Cancellation released textures before GUI completed");
  complete(queue,acquired);require(!waiting.get(),"Retired frame was submitted");queue.consume(acquired);
  queue.enable(0);
  auto broken=queue.post(0,50);require(queue.acquire()==broken,"Missing broken frame");
  queue.close();queue.abort(broken->serial);require(!queue.wait(broken),"Aborted frame submitted");queue.consume(broken);
  require(!queue.acquire()&&!queue.post(1,60),"Closed queue accepted work");
  std::cout<<"PASS: independent receiver cycles, slow SDK show, bounded FIFO, plane ownership, cancel/acquire/abort safety\n";
  return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
