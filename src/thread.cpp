/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2012 Marco Costalba, Joona Kiiski, Tord Romstad

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <cassert>
#include <functional>
#include <iostream>

#include "movegen.h"
#include "search.h"
#include "thread.h"
#include "ucioption.h"

using namespace Search;

ThreadPool Threads; // Global object


// Thread c'tor starts a newly-created thread of execution that will call
// the idle loop function pointed by 'fn' going immediately to sleep.

Thread::Thread(Fn fn) : splitPoints() {

  is_searching = do_exit = false;
  maxPly = splitPointsCnt = 0;
  curSplitPoint = nullptr;
  idx = Threads.size();

  do_sleep = (fn != &Thread::main_loop); // Avoid a race with start_searching()

  nativeThread = std::thread(std::mem_fun(fn), this); // Starts and calls 'fn'
}


// Thread d'tor waits for thread termination before to return.

Thread::~Thread() {

  assert(do_sleep);

  do_exit = true; // Search must be already finished
  wake_up();
  nativeThread.join();
}


// Thread::timer_loop() is where the timer thread waits maxPly milliseconds and
// then calls check_time(). If maxPly is 0 thread sleeps until is woken up.
extern void check_time();

void Thread::timer_loop() {

  while (!do_exit)
  {
      std::unique_lock<std::mutex> lk(mutex);
      sleepCondition.wait_for(lk, std::chrono::milliseconds(maxPly ? maxPly : INT_MAX));
      check_time();
  }
}


// Thread::main_loop() is where the main thread is parked waiting to be started
// when there is a new search. Main thread will launch all the slave threads.

void Thread::main_loop() {

  while (true)
  {
      std::unique_lock<std::mutex> lk(mutex);

      do_sleep = true; // Always return to sleep after a search
      is_searching = false;

      while (do_sleep && !do_exit)
      {
          Threads.sleepCondition.notify_one(); // Wake up UI thread if needed
          sleepCondition.wait(lk);
      }

      lk.unlock();

      if (do_exit)
          return;

      is_searching = true;

      Search::think();

      assert(is_searching);
  }
}


// Thread::wake_up() wakes up the thread, normally at the beginning of the search
// or, if "sleeping threads" is used at split time.

void Thread::wake_up() {

  std::unique_lock<std::mutex> lk(mutex);
  sleepCondition.notify_one();
}


// Thread::wait_for_stop() is called when the maximum depth is reached while
// the program is pondering. The point is to work around a wrinkle in the UCI
// protocol: When pondering, the engine is not allowed to give a "bestmove"
// before the GUI sends it a "stop" or "ponderhit" command. We simply wait here
// until one of these commands (that raise Signals.stop) is sent and
// then return, after which the bestmove and pondermove will be printed.

void Thread::wait_for_stop() {

  std::unique_lock<std::mutex> lk(mutex);
  sleepCondition.wait(lk, []{ return Signals.stop; });
}


// Thread::cutoff_occurred() checks whether a beta cutoff has occurred in the
// current active split point, or in some ancestor of the split point.

bool Thread::cutoff_occurred() const {

  for (SplitPoint* sp = curSplitPoint; sp; sp = sp->parent)
      if (sp->cutoff)
          return true;

  return false;
}


// Thread::is_available_to() checks whether the thread is available to help the
// thread 'master' at a split point. An obvious requirement is that thread must
// be idle. With more than two threads, this is not sufficient: If the thread is
// the master of some active split point, it is only available as a slave to the
// slaves which are busy searching the split point at the top of slaves split
// point stack (the "helpful master concept" in YBWC terminology).

bool Thread::is_available_to(Thread* master) const {

  if (is_searching)
      return false;

  // Make a local copy to be sure doesn't become zero under our feet while
  // testing next condition and so leading to an out of bound access.
  int spCnt = splitPointsCnt;

  // No active split points means that the thread is available as a slave for any
  // other thread otherwise apply the "helpful master" concept if possible.
  return !spCnt || (splitPoints[spCnt - 1].slavesMask & (1ULL << master->idx));
}


// init() is called at startup. Initializes lock and condition variable and
// launches requested threads sending them immediately to sleep. We cannot use
// a c'tor becuase Threads is a static object and we need a fully initialized
// engine at this point due to allocation of endgames in Thread c'tor.

void ThreadPool::init() {

  timer = new Thread(&Thread::timer_loop);
  threads.push_back(new Thread(&Thread::main_loop));
  read_uci_options();
}


// exit() cleanly terminates the threads before the program exits.

void ThreadPool::exit() {

  delete timer; // As first becuase check_time() accesses threads data

  for (Thread* th : threads)
      delete th;
}


// read_uci_options() updates internal threads parameters from the corresponding
// UCI options and creates/destroys threads to match the requested number. Thread
// objects are dynamically allocated to avoid creating in advance all possible
// threads, with included pawns and material tables, if only few are used.

void ThreadPool::read_uci_options() {

  maxThreadsPerSplitPoint = Options["Max Threads per Split Point"];
  minimumSplitDepth       = Options["Min Split Depth"] * ONE_PLY;
  useSleepingThreads      = Options["Use Sleeping Threads"];
  size_t requested        = Options["Threads"];

  assert(requested > 0);

  while (threads.size() < requested)
      threads.push_back(new Thread(&Thread::idle_loop));

  while (threads.size() > requested)
  {
      delete threads.back();
      threads.pop_back();
  }
}


// wake_up() is called before a new search to start the threads that are waiting
// on the sleep condition and to reset maxPly. When useSleepingThreads is set
// threads will be woken up at split time.

void ThreadPool::wake_up() const {

  for (Thread* th : threads)
  {
      th->maxPly = 0;
      th->do_sleep = false;

      if (!useSleepingThreads)
          th->wake_up();
  }
}


// sleep() is called after the search finishes to ask all the threads but the
// main one to go waiting on a sleep condition.

void ThreadPool::sleep() const {

  for (Thread* th : threads)
      if (th->idx != 0) // Not main thread to avoid a race with start_searching()
          th->do_sleep = true;
}


// available_slave_exists() tries to find an idle thread which is available as
// a slave for the thread 'master'.

bool ThreadPool::available_slave_exists(Thread* master) const {

  for (Thread* th : threads)
      if (th->is_available_to(master))
          return true;

  return false;
}


// split() does the actual work of distributing the work at a node between
// several available threads. If it does not succeed in splitting the node
// (because no idle threads are available, or because we have no unused split
// point objects), the function immediately returns. If splitting is possible, a
// SplitPoint object is initialized with all the data that must be copied to the
// helper threads and then helper threads are told that they have been assigned
// work. This will cause them to instantly leave their idle loops and call
// search(). When all threads have returned from search() then split() returns.

template <bool Fake>
Value ThreadPool::split(Position& pos, Stack* ss, Value alpha, Value beta,
                        Value bestValue, Move* bestMove, Depth depth, Move threatMove,
                        int moveCount, MovePicker& mp, int nodeType) {

  assert(pos.pos_is_ok());
  assert(bestValue > -VALUE_INFINITE);
  assert(bestValue <= alpha);
  assert(alpha < beta);
  assert(beta <= VALUE_INFINITE);
  assert(depth > DEPTH_ZERO);

  Thread* master = pos.this_thread();

  if (master->splitPointsCnt >= MAX_SPLITPOINTS_PER_THREAD)
      return bestValue;

  // Pick the next available split point from the split point stack
  SplitPoint& sp = master->splitPoints[master->splitPointsCnt];

  sp.parent = master->curSplitPoint;
  sp.master = master;
  sp.cutoff = false;
  sp.slavesMask = 1ULL << master->idx;
  sp.depth = depth;
  sp.bestMove = *bestMove;
  sp.threatMove = threatMove;
  sp.alpha = alpha;
  sp.beta = beta;
  sp.nodeType = nodeType;
  sp.bestValue = bestValue;
  sp.mp = &mp;
  sp.moveCount = moveCount;
  sp.pos = &pos;
  sp.nodes = 0;
  sp.ss = ss;

  assert(master->is_searching);

  master->curSplitPoint = &sp;
  int slavesCnt = 0;

  // Try to allocate available threads and ask them to start searching setting
  // is_searching flag. This must be done under lock protection to avoid concurrent
  // allocation of the same slave by another master.
  mutex.lock();
  sp.mutex.lock();

  for (Thread* th : threads)
      if (th->is_available_to(master) && !Fake)
      {
          sp.slavesMask |= 1ULL << th->idx;
          th->curSplitPoint = &sp;
          th->is_searching = true; // Slave leaves idle_loop()

          if (useSleepingThreads)
              th->wake_up();

          if (++slavesCnt + 1 >= maxThreadsPerSplitPoint) // Master is always included
              break;
      }

  master->splitPointsCnt++;

  sp.mutex.unlock();
  mutex.unlock();

  // Everything is set up. The master thread enters the idle loop, from which
  // it will instantly launch a search, because its is_searching flag is set.
  // The thread will return from the idle loop when all slaves have finished
  // their work at this split point.
  if (slavesCnt || Fake)
  {
      master->idle_loop();

      // In helpful master concept a master can help only a sub-tree of its split
      // point, and because here is all finished is not possible master is booked.
      assert(!master->is_searching);
  }

  // We have returned from the idle loop, which means that all threads are
  // finished. Note that setting is_searching and decreasing splitPointsCnt is
  // done under lock protection to avoid a race with Thread::is_available_to().
  mutex.lock();
  sp.mutex.lock();

  master->is_searching = true;
  master->splitPointsCnt--;
  master->curSplitPoint = sp.parent;
  pos.set_nodes_searched(pos.nodes_searched() + sp.nodes);
  *bestMove = sp.bestMove;

  sp.mutex.unlock();
  mutex.unlock();

  return sp.bestValue;
}

// Explicit template instantiations
template Value ThreadPool::split<false>(Position&, Stack*, Value, Value, Value, Move*, Depth, Move, int, MovePicker&, int);
template Value ThreadPool::split<true>(Position&, Stack*, Value, Value, Value, Move*, Depth, Move, int, MovePicker&, int);


// set_timer() is used to set the timer to trigger after msec milliseconds.
// If msec is 0 then timer is stopped.

void ThreadPool::set_timer(int msec) {

  std::unique_lock<std::mutex> lk(timer->mutex);
  timer->maxPly = msec;
  timer->sleepCondition.notify_one(); // Wake up and restart the timer
}


// wait_for_search_finished() waits for main thread to go to sleep, this means
// search is finished. Then returns.

void ThreadPool::wait_for_search_finished() {

  Thread* t = main_thread();
  std::unique_lock<std::mutex> lk(t->mutex);
  sleepCondition.wait(lk, [&]{ return t->do_sleep; });
}


// start_searching() wakes up the main thread sleeping in  main_loop() so to start
// a new search, then returns immediately.

void ThreadPool::start_searching(const Position& pos, const LimitsType& limits,
                                 const std::vector<Move>& searchMoves, StateStackPtr& states) {
  wait_for_search_finished();

  SearchTime = Time::now(); // As early as possible

  Signals.stopOnPonderhit = Signals.firstRootMove = false;
  Signals.stop = Signals.failedLowAtRoot = false;

  RootPos = pos;
  Limits = limits;
  SetupStates = std::move(states); // Ownership transfer here
  RootMoves.clear();

  for (const MoveStack& ms : MoveList<LEGAL>(pos))
      if (searchMoves.empty() || count(searchMoves.begin(), searchMoves.end(), ms.move))
          RootMoves.push_back(RootMove(ms.move));

  main_thread()->do_sleep = false;
  main_thread()->wake_up();
}
