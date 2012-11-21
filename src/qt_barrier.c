#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

// a logrithmic self-cleaning barrier -- requires the size to be known
// Follows a strategy that the MTA used - so might work better if it used the
// full/empty implementation, but orginal developement predated it's inclusion
// here and had no available F/E implementation - 12/17/09 akp

#include <stdlib.h>
#include "qthread-int.h"
#include <stdio.h>

#include "qt_barrier.h"
#include "qt_asserts.h"
#include "qt_atomics.h"
#include "qthread/qthread.h"
#include "qt_debug.h"
#include "qt_visibility.h"
#include "qthread_innards.h" /* for qthread_library_initialized */

struct qt_barrier_s {
    int        count;           // size of barrier
    size_t     activeSize;      // size of barrier (KBW: redundant???)
    size_t     allocatedSize;   // lowest size power of 2 equal or bigger than barrier -- for allocations
    int        doneLevel;       // height of the tree
    char       barrierDebug;    // flag to turn on internal printf debugging
    syncvar_t *upLock;
    //    int64_t *upLock;	// array of counters to track number of people that have arrived

    syncvar_t *downLock;
    //    int64_t *downLock;	// array of counters that allows threads to leave
} /* qt_barrier_t */;

static void qtb_internal_initialize_variable(qt_barrier_t *b,
                                             size_t        size,
                                             int           debug);
static void qtb_internal_initialize_fixed(qt_barrier_t *b,
                                          size_t        size,
                                          int           debug);

static void qtb_internal_up(qt_barrier_t *b,
                            int           myLock,
                            int64_t       val,
                            int           level);
static void qtb_internal_down(qt_barrier_t *b,
                              int           mylock,
                              int           level);

/* global barrier */
qt_barrier_t *MBar = NULL;

void API_FUNC qt_barrier_resize(size_t size)
{                                      /*{{{ */
    assert(qthread_library_initialized);
    qt_barrier_destroy(MBar);
    MBar = NULL;
    qt_global_barrier_init(size, 0);
}                                      /*}}} */

void API_FUNC qt_barrier_destroy(qt_barrier_t *b)
{                                      /*{{{ */
    assert(qthread_library_initialized);
    assert(b);
    if (b->upLock) {
        free((void *)(b->upLock));
    }
    if (b->downLock) {
        free((void *)(b->downLock));
    }
    free(b);
}                                      /*}}} */

qt_barrier_t API_FUNC *qt_barrier_create(int              size,
                                         qt_barrier_btype type,
                                         int              debug)
{                                      /*{{{ */
    qt_barrier_t *b = calloc(1, sizeof(qt_barrier_t));

    qthread_debug(BARRIER_CALLS, "size(%i), type(%i), debug(%i): begin\n", size,
                  (int)type, debug);
    assert(b);
    if (b) {
        assert(type == REGION_BARRIER);
        switch (type) {
            case REGION_BARRIER:
                qtb_internal_initialize_fixed(b, size, debug);
                break;
            default:
                qtb_internal_initialize_variable(b, size, debug);
                break;
        }
    }
    return b;
}                                      /*}}} */

static void qtb_internal_initialize_variable(qt_barrier_t *b,
                                             size_t        size,
                                             int           debug)
{                                      /*{{{ */
    assert(0);
    printf("Loop barrier not implemented yet\n");
}                                      /*}}} */

static void qtb_internal_initialize_fixed(qt_barrier_t *b,
                                          size_t        size,
                                          int           debug)
{                                      /*{{{ */
    int depth = 1;
    int temp  = size - 1;

    assert(b);
    b->activeSize   = size;
    b->barrierDebug = (char)debug;
    b->count        = size;

    if (size < 1) {
        return;
    }

    // compute size of barrier arrays
    temp >>= 1;
    while (temp) {
        temp >>= 1;
        depth++;                       // how many bits set
    }

    b->doneLevel     = depth;
    b->allocatedSize = (2 << depth);

    // allocate and init upLock and downLock arrays
    b->upLock   = calloc(b->allocatedSize, sizeof(syncvar_t));
    b->downLock = calloc(b->allocatedSize, sizeof(syncvar_t));
    //    b->upLock = calloc(b->allocatedSize, sizeof(int64_t));
    //    b->downLock = calloc(b->allocatedSize, sizeof(int64_t));

    for (size_t i = 0; i < b->activeSize; i++) {
        qthread_syncvar_empty(&b->upLock[i]);
        qthread_syncvar_empty(&b->downLock[i]);
    }
}                                      /*}}} */

// dump function for debugging -  print barrier array contents
void qt_barrier_dump(qt_barrier_t *b,
                     enum dumpType dt)
{                                      /*{{{ */
    size_t       i, j;
    const size_t activeSize = b->activeSize;

    if ((dt == UPLOCK) || (dt == BOTHLOCKS)) {
        printf("upLock\n");
        for (j = 0; j < activeSize; j += 8) {
            for (i = 0; ((i < 8) && ((j + i) < activeSize)); i++) {
                //		printf("%ld ", (long int)b->upLock[j + i]);
            }
            printf("\n");
        }
    }
    if ((dt == DOWNLOCK) || (dt == BOTHLOCKS)) {
        printf("downLock\n");
        for (j = 0; j < activeSize; j += 8) {
            for (i = 0; ((i < 8) && ((j + i) < activeSize)); i++) {
                //		printf("%ld ", (long int)b->downLock[j + i]);
            }
            printf("\n");
        }
    }
}                                      /*}}} */

// walk down the barrier -- releases all locks in subtree below myLock
//    level -- how high in the tree is this node
uint64_t dummy = 0;
static void qtb_internal_down(qt_barrier_t *b,
                              int           myLock,
                              int           level)
{                                      /*{{{ */
    assert(b->activeSize > 1);
    for (int i = level; i >= 0; i--) {
        int mask       = 1 << i;
        int pairedLock = myLock ^ mask;
        if (pairedLock < b->activeSize) {       // my pair is in range
            if (pairedLock > myLock) {
                qthread_syncvar_writeEF(&b->downLock[pairedLock], &dummy);
                if (b->barrierDebug) {
                    printf("\t down lock %d level %d \n", pairedLock, i);
                }
            }
        } else {                       // out of range -- continue
        }
    }
}                                      /*}}} */

// walk up the barrier -- waits for neighbor lock at each level of the tree
//   when both arrive the lower thread number climbs up the tree and the
//   higher number waits for the down walk to release.  When all of the nodes arrive
//   Thread 0 starts the walk down (after executing functions to resync for loop
//   implementation).  Any node that is waiting release at a level other than the
//   leaves, releases its subtree
static void qtb_internal_up(qt_barrier_t *b,
                            int           myLock,
                            int64_t       val,
                            int           level)
{                                      /*{{{ */
    // compute neighbor node at this level
    int  mask       = 1 << level;
    int  pairedLock = myLock ^ mask;
    char debug      = b->barrierDebug;

    assert(b->activeSize > 1);
    if (debug) {
        printf("on lock %d paired with %d level %d val %ld\n", myLock,
               pairedLock, level, (long int)val);
    }
    if (pairedLock >= b->activeSize) { // my pair is out of range don't wait for it
        qthread_syncvar_writeEF(&b->upLock[myLock], &dummy);
        qthread_syncvar_readFE(NULL, &b->downLock[myLock]);
        if (debug) {
            printf("released (no pair) lock %d level %d val %ld\n", myLock,
                   level, (long int)val);
        }
        if (level != 0) {
            qtb_internal_down(b, myLock, level); // everyone is here and I have people to release
        }
        return;                        // done
    }

    if (pairedLock < myLock) {         // I'm higher -- wait for release
        qthread_syncvar_writeEF(&b->upLock[myLock], &dummy);
        if (debug) {
            printf
                ("about to wait on lock %d paired with %d level %d val %ld\n",
                myLock, pairedLock, level, (long int)val);
        }
        qthread_syncvar_readFE(NULL, &b->downLock[myLock]);

        if (debug) {
            printf("released lock %d level %d val %ld\n", myLock, level,
                   (long int)val);
        }
        if (level != 0) {
            qtb_internal_down(b, myLock, level); // everyone is here and I have people to release
        }
        return;                        // done
    } else {                           // I'm lower -- wait for pair and continue up
        if (debug) {
            printf("wait lock %d for %d level %d val %ld\n", myLock,
                   pairedLock, level, (long int)val);
        }
        qthread_syncvar_readFE(NULL, &b->upLock[pairedLock]);
        if (debug) {
            printf("continue on %d level %d val %ld\n", myLock, level,
                   (long int)val);
        }
    }

    if ((level + 1 < b->doneLevel) || (myLock != 0)) {  // not done?  yes
        qtb_internal_up(b, myLock, val, level + 1);
    } else {                           // done -- start release
        // myLock == 0 here -- don't need to fill either because Zero never waits on them
        // qthread_syncvar_writeEF(&b->upLock[myLock],&dummy);
        // qthread_syncvar_writeEF(&b->downLock[myLock],&dummy);
        if (debug) {
            printf("\t start down lock %d level %d \n", myLock, level);
        }
        qtb_internal_down(b, myLock, level);    // everyone is here
    }
}                                      /*}}} */

// actual barrier entry point

void API_FUNC qt_barrier_enter(qt_barrier_t         *b,
                               qthread_shepherd_id_t shep)
{                                      /*{{{ */
    // should be dual versions  1) all active threads barrier
    //                          2) all active streams

    // only dealing with (1) for first pass for now
    int64_t val = 0;

    //    int64_t val = b->upLock[shep] + 1;

    if (b->activeSize <= 1) { return; }
    qtb_internal_up(b, shep, val, 0);
}                                      /*}}} */

// used indirectly by omp barrier calls (initially - others I hope)
// akp 7/24/09
#define QT_GLOBAL_LOGBARRIER
#ifdef QT_GLOBAL_LOGBARRIER
void INTERNAL qt_global_barrier(void)
{                                      /*{{{ */
# ifdef QTHREAD_MULTITHREADED_SHEPHERDS
    const qthread_worker_id_t workerid = qthread_worker(NULL);
    qt_barrier_enter(MBar, workerid);
# else
    const qthread_shepherd_id_t shep = qthread_shep();
    qt_barrier_enter(MBar, shep);
# endif
}                                      /*}}} */

// allow barrer initization from C
void INTERNAL qt_global_barrier_init(int size,
                                     int debug)
{                                      /*{{{ */
    if (MBar == NULL) {
        MBar = qt_barrier_create(size, REGION_BARRIER, debug);
        assert(MBar);
    }
}                                      /*}}} */

void INTERNAL qt_global_barrier_destroy()
{                                      /*{{{ */
    if (MBar) {
        qt_barrier_destroy(MBar);
        MBar = NULL;
    }
}                                      /*}}} */

#endif /* ifdef QT_GLOBAL_LOGBARRIER */
/* vim:set expandtab: */
