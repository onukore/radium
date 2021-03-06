#ifndef _RADIUM_COMMON_QUEUES_HPP
#define _RADIUM_COMMON_QUEUES_HPP


#include <boost/version.hpp>
#if (BOOST_VERSION < 100000) || ((BOOST_VERSION / 100 % 1000) < 58)
  #error "Boost too old. Need at least 1.58.\n Quick fix: cd $HOME ; wget http://downloads.sourceforge.net/project/boost/boost/1.60.0/boost_1_60_0.tar.bz2 ; tar xvjf boost_1_60_0.tar.bz2 (that's it!)"
#endif
#include <boost/lockfree/queue.hpp>

#include "Semaphores.hpp"


// Lockless multithread read / multithread write queue

namespace radium {

template <typename T, int SIZE> struct Queue{

private:
  
  Semaphore ready;
  boost::lockfree::queue< T , boost::lockfree::capacity<SIZE> > queue;

public:
  
  // sets success to false if failed, true if succeeded. Return value is undefined if "success" is false.
  T tryGet(bool &success){
    T ret = 0;
    
    if (ready.tryWait()) {
    
      R_ASSERT(queue.pop(ret));

      success = true;

    } else {

      success = false;

    }
    
    return ret;
  }

  
private:  // Rather not expose this messy (and unsafe) API unless it's needed.
 
  // MUST call either cancelGet() or get_withoutWaiting() afterwards.
  void wait(void){
    ready.wait();
  }

  // Must NOT be called without first calling wait()
  T get_withoutWaiting(){
    T ret;
    memset(&ret, 0, sizeof(T));
    
    R_ASSERT(queue.pop(ret));

    return ret;
  }

  // Must NOT be called without first calling wait().
  void cancelGet(void){
    ready.signal();
  }

public:
  
  // Waits until available. Never returns NULL. Same as calling wait() and get_withoutWaiting() in a row
  T get(void){
    wait();
    return get_withoutWaiting();
  }

  // Same as get, but instead of calling semaphore.wait, we as seamphore.tryWait again and again until it's there. (i.e. we are spinning)
  T buzyGet(void){
    for(;;){
      bool gotit;
      T ret = tryGet(gotit);
      if (gotit)
        return ret;
    }
  }
                 
  void putWithoutSignal(T t) {
    while (!queue.bounded_push(t));
  }

  void signal(int num){
    ready.signal(num);
  }
  
  // returns false if queue was full
  bool tryPut(T t){
    if (queue.bounded_push(t)) {
      ready.signal();
      return true;
    }

    return false;
  }

  // If queue is full, buzy-waits until space is available. Same as calling while(!tryPut(t));
  // (it's simple to create a put which waits on a semaphore instead of buzy-looping, but that functionality hasn't been needed in Radium so far)
  void put(T t){
    while(!tryPut(t));
  }

  int size(void){
    return ready.numSignallers();
  }
};

}

  
#endif
