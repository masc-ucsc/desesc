
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "Report.h"
// #include "ReportGen.h" This file doesn't exist. Replaced it with Report.h
#include "InterConn.h"
#include "ProtocolBase.h"
#include "SescConf.h"

// What does this do? Send some kind of message
class PBTestMsg : public PMessage {
private:
  static pool<PBTestMsg> msgPool;
  friend class pool<PBTestMsg>;

public:
  static PBTestMsg *create(const ProtocolBase *srcPB, const ProtocolBase *dstPB) {
    // ProtocolBase *src;
    // src = (ProtocolBase *)srcPB; //Ehsan
    // ProtocolBase *dst=dstPB;  //Ehsan
    PBTestMsg *msg = msgPool.out();
    msg->setupMessage((ProtocolBase *)srcPB, (ProtocolBase *)dstPB, TestMsg);
    //  msg->setupMessage((ProtocolBase *)1, (ProtocolBase *)2, TestMsg);

    return msg;
  };

  void garbageCollect() { msgPool.in(this); };
};

// What does this do? Send some kind of ACK message
class PBTestAckMsg : public PMessage {
private:
  static pool<PBTestAckMsg> msgPool;
  friend class pool<PBTestAckMsg>;

public:
  static PBTestAckMsg *create(const ProtocolBase *srcPB, const ProtocolBase *dstPB) {
    PBTestAckMsg *msg = msgPool.out();
    msg->setupMessage((ProtocolBase *)srcPB, (ProtocolBase *)dstPB, TestAckMsg);
    // edited by Ehsan (added typecast of srcPB and dstPB to( ProtocolBase *))
    return msg;
  };
  void garbageCollect() { msgPool.in(this); };
};

// This is just some integer floating around in here
// between the classes. Looks like some kind of counter
// for the number of messages
int32_t nMessages;

// Ok, a new class. What is ProtocolA?
// And is the stuff under it part of the class? Just
// Based on the indents, it looks like it.
class ProtocolA : public ProtocolBase {
public:
  ProtocolA(InterConnection *net, RouterID_t rID) : ProtocolBase(net, rID) {
    ProtocolCBBase *pcb = new ProtocolCB<ProtocolA, &ProtocolA::TestHandler>(this);
    registerHandler(pcb, TestMsg);

    pcb = new ProtocolCB<ProtocolA, &ProtocolA::TestAckHandler>(this);
    registerHandler(pcb, TestAckMsg);

    pcb = new ProtocolCB<ProtocolA, &ProtocolA::defaultHandler>(this);
    registerHandler(pcb, DefaultMessage);
  };

  void defaultHandler(Message *msg) {
    I(0);
    nMessages++;
    msg->garbageCollect();
  };

  void TestHandler(Message *m) {
    nMessages++;
    PBTestMsg     *msg   = static_cast<PBTestMsg *>(m);
    static uint8_t conta = 0;
    conta++;

    I(this == msg->getDstPB());
    if (conta & 1) {
      sendMsg(2, PBTestAckMsg::create(this, msg->getSrcPB()));
    } else if (conta & 2) {
      sendMsg(PBTestAckMsg::create(this, msg->getSrcPB()));
    }
    msg->garbageCollect();
  };

  void TestAckHandler(Message *msg) {
    nMessages++;
    msg->garbageCollect();
  };

  // Added semicolon here--not sure if this is the right spot or not
};

pool<PBTestMsg>    PBTestMsg::msgPool(1024);
pool<PBTestAckMsg> PBTestAckMsg::msgPool(1024);

#define NMSG        100000
#define TIME_BUBBLE 128

std::vector<ProtocolA *> pa;

InterConnection *net;
size_t           nRouters;

Time_t  stClock;
timeval stTime;
timeval endTime;

void startBench() {
  gettimeofday(&stTime, 0);
  stClock   = globalClock;
  nMessages = 0;
};

void endBench(const char *str) {
  gettimeofday(&endTime, 0);

  double msecs = (endTime.tv_sec - stTime.tv_sec) * 1000 + (endTime.tv_usec - stTime.tv_usec) / 1000;

  fprintf(stderr,
          "%s: %8.2f KPackets/s or %8.2f Kclks/s\n",
          str,
          (double)nMessages / msecs,
          (double)(globalClock - stClock) / msecs);
};

void bench1() {
  // Two neighbours messages

  for (int32_t i = 0; i < NMSG; i++) {
    Message *msg = PBTestMsg::create(pa[nRouters / 2], pa[nRouters / 2 + 1]);
    //   Message *msg = PBTestMsg::create(pa[1],pa[2]);
    net->sendMsg(msg);
    EventScheduler::advanceClock();
    if ((i % 128) == 0) {
      for (int32_t k = 0; k < TIME_BUBBLE; k++) {
        EventScheduler::advanceClock();
      }
    }
  }

  for (int32_t k = 0; k < TIME_BUBBLE; k++) {
    EventScheduler::advanceClock();
  }
};

void bench2() {
  // Lots of simultaneous messages

  for (size_t i = 0; i < NMSG / nRouters; i++) {
    for (size_t k = 0; k < nRouters; k++) {
      //  Message *msg = PBTestMsg::create(pa[(k*7) % nRouters]
               ,pa[(k*3)% nRouters]);
               Message *msg = PBTestMsg::create(pa[(k * 5) % nRouters], pa[(k * 3) % nRouters]);

               net->sendMsg(msg);
    }
    EventScheduler::advanceClock();
    if ((i % 32) == 0) {
      for (int32_t k = 0; k < TIME_BUBBLE; k++) {
        EventScheduler::advanceClock();
      }
    }
  }

  for (int32_t k = 0; k < 4 * TIME_BUBBLE; k++) {
    EventScheduler::advanceClock();
  }
};

/* Version 1 from Ehsan--fails at different point in gdb
int32_t main(int32_t argc, char **argv, char **envp)
{
  if (argc<2) {
    fprintf(stderr,"Usage:\n\t%s <router.conf>\n", argv[0]);
    exit(0);
  }

  Report::openFile("netBench.log");

  SescConf = new SConfig(argc, (const char **)argv);   // First thing to do

 */

// Version 3 from Ehsan
int32_t main(int32_t argc, char **argv, char **envp) {
  if (argc < 2) {
    fprintf(stderr, "Usage:\n\t%s <router.conf>\n", argv[0]);
    exit(0);
  }

  Report::openFile("netBench.log");
  // edited by ehsan
  // SescConf = new SConfig(argv[1]);   // First thing to do
  SescConf = new SConfig(argc, (const char **)argv);

  net = new InterConnection("network1");

  nRouters = net->getnRouters();

  for (size_t i = 0; i < nRouters; i++) {
    ProtocolA *p = new ProtocolA(net, i);
    pa.push_back(p);
  }

  printf("System ready\n");

  fprintf(stderr, "Bench two neighbours message...");
  startBench();
  bench1();
  fprintf(stderr, "done\n");
  endBench("bench1");

  fprintf(stderr, "Bench heavy traffic...");
  startBench();
  bench2();
  fprintf(stderr, "done\n");
  endBench("bench2");

  fprintf(stderr, "Mix Benchmark...");
  startBench();
  bench2();
  printf("one\n");
  bench2();
  printf("two\n");
  bench1();
  printf("three\n");
  bench2();
  fprintf(stderr, "done\n");
  endBench("bench3");

  for (int32_t k = 0; k < TIME_BUBBLE * 100; k++) {
    EventScheduler::advanceClock();
  }

  Report::close();
}
