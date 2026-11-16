// threadtest.cc
//      Simple test case for the threads assignment.
//
//      Create two threads, and have them context switch
//      back and forth between themselves by calling Thread::Yield,
//      to illustratethe inner workings of the thread system.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"
#if defined(CHANGED) && defined(THREADS)
#include "synch.h"
#endif

// testnum is set in main.cc
int testnum = 1;

#if defined(CHANGED) && defined(THREADS)

//----------------------------------------------------------------------
// SimpleThread (HW1)
//      Each thread increments SharedVariable 5 times. Without
//      HW1_SEMAPHORES or HW1_LOCKS the accesses are unsynchronized.
//      With either, the read-modify-write is protected and a barrier
//      makes every thread wait for the others before reading the
//      final value.
//----------------------------------------------------------------------

int SharedVariable;

#if defined(HW1_LOCKS)
Lock *sharedLock;     // protects SharedVariable and numDone
int numThreads;       // total threads running SimpleThread (n + 1)
int numDone;          // how many have finished the loop
#elif defined(HW1_SEMAPHORES)
Semaphore *mutex;     // protects SharedVariable and numDone
Semaphore *barrier;   // holds threads until all have finished the loop
int numThreads;       // total threads running SimpleThread (n + 1)
int numDone;          // how many have finished the loop
#endif

void
SimpleThread(int which)
{
    int num, val;

    for (num = 0; num < 5; num++) {
#if defined(HW1_LOCKS)
        sharedLock->Acquire();
#elif defined(HW1_SEMAPHORES)
        mutex->P();
#endif
        val = SharedVariable;
        printf("*** thread %d sees value %d\n", which, val);
        currentThread->Yield();
        SharedVariable = val+1;
#if defined(HW1_LOCKS)
        sharedLock->Release();
#elif defined(HW1_SEMAPHORES)
        mutex->V();
#endif
        currentThread->Yield();
    }

#if defined(HW1_LOCKS)
    // Barrier: record that we're done, then yield until all are done.
    sharedLock->Acquire();
    numDone++;
    sharedLock->Release();
    for (;;) {
        int allDone;
        sharedLock->Acquire();
        allDone = (numDone == numThreads);
        sharedLock->Release();
        if (allDone)
            break;
        currentThread->Yield();
    }
#elif defined(HW1_SEMAPHORES)
    mutex->P();
    numDone++;
    if (numDone == numThreads)
        barrier->V();         // last thread to finish opens the turnstile
    mutex->V();
    barrier->P();             // wait for the turnstile
    barrier->V();             // pass it on to the next waiting thread
#endif

    val = SharedVariable;
    printf("Thread %d sees final value %d\n", which, val);
}

#ifdef HW1_CONDTEST
//----------------------------------------------------------------------
// CondTest
//      Temporary test of Condition variables (not part of the
//      required output). Producer/consumer exercises Wait and
//      Signal; the second part checks that Broadcast wakes everyone.
//----------------------------------------------------------------------

#define CT_BUF_SIZE 3
#define CT_NUM_ITEMS 10

Lock *ctLock;
Condition *ctNotFull, *ctNotEmpty, *ctGo;
int ctBuffer[CT_BUF_SIZE];
int ctCount = 0, ctIn = 0, ctOut = 0;
int ctGoFlag = 0, ctNumWaiting = 0, ctNumReleased = 0;

void
CtProducer(int unused)
{
    for (int i = 0; i < CT_NUM_ITEMS; i++) {
        ctLock->Acquire();
        while (ctCount == CT_BUF_SIZE)
            ctNotFull->Wait(ctLock);
        ctBuffer[ctIn] = i;
        ctIn = (ctIn + 1) % CT_BUF_SIZE;
        ctCount++;
        printf("produced %d (count %d)\n", i, ctCount);
        ctNotEmpty->Signal(ctLock);
        ctLock->Release();
    }
}

void
CtConsumer(int unused)
{
    for (int i = 0; i < CT_NUM_ITEMS; i++) {
        ctLock->Acquire();
        while (ctCount == 0)
            ctNotEmpty->Wait(ctLock);
        int item = ctBuffer[ctOut];
        ctOut = (ctOut + 1) % CT_BUF_SIZE;
        ctCount--;
        printf("consumed %d (count %d)\n", item, ctCount);
        ASSERT(item == i);                 // items arrive in order
        ctNotFull->Signal(ctLock);
        ctLock->Release();
    }
}

void
CtWaiter(int which)
{
    ctLock->Acquire();
    ctNumWaiting++;
    while (!ctGoFlag)
        ctGo->Wait(ctLock);
    ctNumReleased++;
    printf("waiter %d released\n", which);
    ctLock->Release();
}

void
CondTest(int n)
{
    ctLock = new Lock("condtest lock");
    ctNotFull = new Condition("not full");
    ctNotEmpty = new Condition("not empty");
    ctGo = new Condition("go");

    printf("--- producer/consumer test ---\n");
    Thread *t = new Thread("producer");
    t->Fork(CtProducer, 0);
    CtConsumer(0);                         // main thread is the consumer

    printf("--- broadcast test with %d waiters ---\n", n);
    for (int i = 1; i <= n; i++) {
        t = new Thread("waiter");
        t->Fork(CtWaiter, i);
    }
    for (;;) {                             // wait until all are in Wait()
        int allWaiting;
        ctLock->Acquire();
        allWaiting = (ctNumWaiting == n);
        if (allWaiting) {
            ctGoFlag = 1;
            ctGo->Broadcast(ctLock);
        }
        ctLock->Release();
        if (allWaiting)
            break;
        currentThread->Yield();
    }
    for (int k = 0; k < 1000 && ctNumReleased < n; k++)
        currentThread->Yield();
    printf("broadcast released %d of %d waiters\n", ctNumReleased, n);
}
#endif
//----------------------------------------------------------------------
// ThreadTest (HW1)
//      Fork n new threads running SimpleThread, then run
//      SimpleThread(0) in the current thread (n + 1 threads total).
//----------------------------------------------------------------------

void
ThreadTest(int n)
{
    DEBUG('t', "Entering ThreadTest");

#ifdef HW1_CONDTEST
    CondTest(n);
    return;
#endif

#if defined(HW1_LOCKS)
    sharedLock = new Lock("SharedVariable lock");
    numThreads = n + 1;
    numDone = 0;
#elif defined(HW1_SEMAPHORES)
    mutex = new Semaphore("mutex", 1);
    barrier = new Semaphore("barrier", 0);
    numThreads = n + 1;
    numDone = 0;
#endif

    for (int i = 1; i <= n; i++) {
        Thread *t = new Thread("forked thread");
        t->Fork(SimpleThread, i);
    }
    SimpleThread(0);
}

//----------------------------------------------------------------------
// Elevator (HW1 Exercise 4)
//      One elevator thread serves floors 1..numFloors; each person is
//      a thread. All shared state is protected by elevatorLock.
//      The elevator sweeps in one direction while there are requests
//      ahead, then reverses, stopping wherever someone needs to get on
//      or off. It holds at most ELEVATOR_CAPACITY people.
//----------------------------------------------------------------------

#define ELEVATOR_CAPACITY 5
#define TICKS_PER_FLOOR 50

struct PersonThread {
    int id;
    int atFloor;
    int toFloor;
};

struct ElevatorThread {
    int numFloors;
    int currentFloor;
    int numPeopleIn;
};

static struct ElevatorThread elevator;
static Lock *elevatorLock = NULL;    // protects all elevator state below
static Condition *elevatorIdle;      // elevator sleeps here when there is no work
static Condition *elevatorDone;      // elevator waits here while people get on/off
static Condition **atFloorCond;      // people wait here for the doors to open at a floor
static int *waitingAt;               // people waiting to get on, per floor
static int *goingTo;                 // riders inside heading to each floor
static int totalWaiting = 0;         // people waiting on all floors
static int stoppedAt = 0;            // floor with doors open, 0 while moving/idle
static int direction = 1;            // +1 going up, -1 going down
static int nextPersonId = 1;

// Does the elevator need to open its doors at this floor?
static int
ElevatorNeedsStop(int floor)
{
    return goingTo[floor] > 0 ||
           (waitingAt[floor] > 0 && elevator.numPeopleIn < ELEVATOR_CAPACITY);
}

// Is anyone waiting at, or riding to, a floor beyond us in direction dir?
static int
ElevatorWorkAhead(int dir)
{
    for (int f = elevator.currentFloor + dir;
         f >= 1 && f <= elevator.numFloors; f += dir)
        if (waitingAt[f] > 0 || goingTo[f] > 0)
            return 1;
    return 0;
}

// Open the doors at this floor and wait until everyone who can get
// off or on has done so.
static void
ElevatorServeFloor(int floor)
{
    stoppedAt = floor;
    while (ElevatorNeedsStop(floor)) {
        atFloorCond[floor]->Broadcast(elevatorLock);
        elevatorDone->Wait(elevatorLock);
    }
    stoppedAt = 0;
}

// Body of the elevator thread.
static void
ElevatorLoop(int unused)
{
    elevatorLock->Acquire();
    for (;;) {
        while (totalWaiting == 0 && elevator.numPeopleIn == 0)
            elevatorIdle->Wait(elevatorLock);

        // Someone is waiting on the floor where we were idle.
        if (ElevatorNeedsStop(elevator.currentFloor)) {
            printf("Elevator arrives on floor %d.\n", elevator.currentFloor);
            ElevatorServeFloor(elevator.currentFloor);
            continue;
        }

        if (!ElevatorWorkAhead(direction))
            direction = -direction;
        ASSERT(ElevatorWorkAhead(direction));

        // Travel one floor: 50 ticks, letting other threads run meanwhile.
        elevatorLock->Release();
        for (int t = 0; t < TICKS_PER_FLOOR; t++)
            currentThread->Yield();
        elevatorLock->Acquire();

        elevator.currentFloor += direction;
        printf("Elevator arrives on floor %d.\n", elevator.currentFloor);
        if (ElevatorNeedsStop(elevator.currentFloor))
            ElevatorServeFloor(elevator.currentFloor);
    }
}

// Body of each person thread.
static void
PersonLoop(int arg)
{
    PersonThread *p = (PersonThread *)arg;

    elevatorLock->Acquire();
    printf("Person %d wants to go to floor %d from floor %d.\n",
           p->id, p->toFloor, p->atFloor);
    waitingAt[p->atFloor]++;
    totalWaiting++;
    elevatorIdle->Signal(elevatorLock);           // wake the elevator if idle

    // Wait for the doors to open here with room inside.
    while (!(stoppedAt == p->atFloor &&
             elevator.numPeopleIn < ELEVATOR_CAPACITY))
        atFloorCond[p->atFloor]->Wait(elevatorLock);
    waitingAt[p->atFloor]--;
    totalWaiting--;
    elevator.numPeopleIn++;
    goingTo[p->toFloor]++;                        // tell it where we're going
    printf("Person %d got into the elevator.\n", p->id);
    elevatorDone->Signal(elevatorLock);

    // Ride until the doors open at our destination.
    while (stoppedAt != p->toFloor)
        atFloorCond[p->toFloor]->Wait(elevatorLock);
    goingTo[p->toFloor]--;
    elevator.numPeopleIn--;
    printf("Person %d got out of the elevator.\n", p->id);
    elevatorDone->Signal(elevatorLock);

    elevatorLock->Release();
    delete p;
}

//----------------------------------------------------------------------
// Elevator
//      Start the elevator thread serving floors 1..numFloors.
//      Called once, before any ArrivingGoingFromTo().
//----------------------------------------------------------------------

void
Elevator(int numFloors)
{
    elevator.numFloors = numFloors;
    elevator.currentFloor = 1;
    elevator.numPeopleIn = 0;

    elevatorLock = new Lock("elevator lock");
    elevatorIdle = new Condition("elevator idle");
    elevatorDone = new Condition("elevator done");
    atFloorCond = new Condition*[numFloors + 1];
    waitingAt = new int[numFloors + 1];
    goingTo = new int[numFloors + 1];
    for (int f = 0; f <= numFloors; f++) {
        atFloorCond[f] = new Condition("elevator floor");
        waitingAt[f] = 0;
        goingTo[f] = 0;
    }

    Thread *t = new Thread("elevator");
    t->Fork(ElevatorLoop, 0);
}

//----------------------------------------------------------------------
// ArrivingGoingFromTo
//      Create a person thread with a unique id who wants to ride
//      from atFloor to toFloor.
//----------------------------------------------------------------------

void
ArrivingGoingFromTo(int atFloor, int toFloor)
{
    PersonThread *p = new PersonThread;
    p->id = nextPersonId++;
    p->atFloor = atFloor;
    p->toFloor = toFloor;

    Thread *t = new Thread("person");
    t->Fork(PersonLoop, (int)p);
}

#else

//----------------------------------------------------------------------
// SimpleThread
//      Loop 5 times, yielding the CPU to another ready thread
//      each iteration.
//
//      "which" is simply a number identifying the thread, for debugging
//      purposes.
//----------------------------------------------------------------------

void
SimpleThread(int which)
{
    int num;

    for (num = 0; num < 5; num++) {
        printf("*** thread %d looped %d times\n", which, num);
        currentThread->Yield();
    }
}

//----------------------------------------------------------------------
// ThreadTest1
//      Set up a ping-pong between two threads, by forking a thread
//      to call SimpleThread, and then calling SimpleThread ourselves.
//----------------------------------------------------------------------

void
ThreadTest1()
{
    DEBUG('t', "Entering ThreadTest1");

    Thread *t = new Thread("forked thread");

    t->Fork(SimpleThread, 1);
    SimpleThread(0);
}

//----------------------------------------------------------------------
// ThreadTest
//      Invoke a test routine.
//----------------------------------------------------------------------

void
ThreadTest()
{
    switch (testnum) {
    case 1:
        ThreadTest1();
        break;
    default:
        printf("No test specified.\n");
        break;
    }
}

#endif