/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2013 Couchbase, Inc
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

#ifndef SRC_DCP_STREAM_H_
#define SRC_DCP_STREAM_H_ 1

#include "config.h"

#include "ep_engine.h"
#include "ext_meta_parser.h"
#include "dcp/dcp-types.h"
#include "dcp/producer.h"
#include "response.h"
#include "vbucket.h"

#include <atomic>
#include <climits>
#include <queue>

class EventuallyPersistentEngine;
class MutationResponse;
class SetVBucketState;
class SnapshotMarker;
class DcpResponse;

enum stream_state_t {
    STREAM_PENDING,
    STREAM_BACKFILLING,
    STREAM_IN_MEMORY,
    STREAM_TAKEOVER_SEND,
    STREAM_TAKEOVER_WAIT,
    STREAM_READING,
    STREAM_DEAD
};

enum end_stream_status_t {
    //! The stream ended due to all items being streamed
    END_STREAM_OK,
    //! The stream closed early due to a close stream message
    END_STREAM_CLOSED,
    //! The stream closed early because the vbucket state changed
    END_STREAM_STATE,
    //! The stream closed early because the connection was disconnected
    END_STREAM_DISCONNECTED,
    //! The stream was closed early because it was too slow (currently unused,
    //! but not deleted because it is part of the externally-visible API)
    END_STREAM_SLOW
};

enum stream_type_t {
    STREAM_ACTIVE,
    STREAM_NOTIFIER,
    STREAM_PASSIVE
};

enum snapshot_type_t {
    none,
    disk,
    memory
};

enum process_items_error_t {
    all_processed,
    more_to_process,
    cannot_process
};

enum backfill_source_t {
    BACKFILL_FROM_MEMORY,
    BACKFILL_FROM_DISK
};

class Stream : public RCValue {
public:
    Stream(const std::string &name, uint32_t flags, uint32_t opaque,
           uint16_t vb, uint64_t start_seqno, uint64_t end_seqno,
           uint64_t vb_uuid, uint64_t snap_start_seqno,
           uint64_t snap_end_seqno);

    virtual ~Stream();

    uint32_t getFlags() { return flags_; }

    uint16_t getVBucket() { return vb_; }

    uint32_t getOpaque() { return opaque_; }

    uint64_t getStartSeqno() { return start_seqno_; }

    uint64_t getEndSeqno() { return end_seqno_; }

    uint64_t getVBucketUUID() { return vb_uuid_; }

    uint64_t getSnapStartSeqno() { return snap_start_seqno_; }

    uint64_t getSnapEndSeqno() { return snap_end_seqno_; }

    stream_state_t getState() { return state_; }

    stream_type_t getType() { return type_; }

    virtual void addStats(ADD_STAT add_stat, const void *c);

    virtual DcpResponse* next() = 0;

    virtual uint32_t setDead(end_stream_status_t status) = 0;

    virtual void notifySeqnoAvailable(uint64_t seqno) {}

    const std::string& getName() {
        return name_;
    }

    virtual void setActive() {
        // Stream defaults to do nothing
    }

    bool isActive() {
        return state_ != STREAM_DEAD;
    }

    /// @Returns true if state_ is Backfilling
    bool isBackfilling() const;

    /// @Returns true if state_ is InMemory
    bool isInMemory() const;

    bool isInTakeoverSend() const;

    void clear() {
        LockHolder lh(streamMutex);
        clear_UNLOCKED();
    }

    /// Return a string describing the given stream state.
    static const char* stateName(stream_state_t st);

protected:

    void clear_UNLOCKED();

    /* To be called after getting streamMutex lock */
    void pushToReadyQ(DcpResponse* resp);

    /* To be called after getting streamMutex lock */
    void popFromReadyQ(void);

    uint64_t getReadyQueueMemory(void);

    const std::string &name_;
    uint32_t flags_;
    uint32_t opaque_;
    uint16_t vb_;
    uint64_t start_seqno_;
    uint64_t end_seqno_;
    uint64_t vb_uuid_;
    uint64_t snap_start_seqno_;
    uint64_t snap_end_seqno_;
    AtomicValue<stream_state_t> state_;
    stream_type_t type_;

    AtomicValue<bool> itemsReady;
    Mutex streamMutex;
    std::queue<DcpResponse*> readyQ;

    // Number of items in the readyQ that are not meta items. Used for
    // calculating getItemsRemaining(). Atomic so it can be safely read by
    // getItemsRemaining() without acquiring streamMutex.
    AtomicValue<size_t> readyQ_non_meta_items;

    const static uint64_t dcpMaxSeqno;

private:
    /* readyQueueMemory tracks the memory occupied by elements
     * in the readyQ.  It is an atomic because otherwise
       getReadyQueueMemory would need to acquire streamMutex.
     */
    AtomicValue <uint64_t> readyQueueMemory;
};


class ActiveStreamCheckpointProcessorTask;

class ActiveStream : public Stream {
public:
    ActiveStream(EventuallyPersistentEngine* e, dcp_producer_t p,
                 const std::string &name, uint32_t flags, uint32_t opaque,
                 uint16_t vb, uint64_t st_seqno, uint64_t en_seqno,
                 uint64_t vb_uuid, uint64_t snap_start_seqno,
                 uint64_t snap_end_seqno);

    ~ActiveStream();

    DcpResponse* next();

    void setActive() {
        LockHolder lh(streamMutex);
        if (state_ == STREAM_PENDING) {
            transitionState(STREAM_BACKFILLING);
        }
    }

    uint32_t setDead(end_stream_status_t status);

    void notifySeqnoAvailable(uint64_t seqno);

    void snapshotMarkerAckReceived();

    void setVBucketStateAckRecieved();

    void incrBackfillRemaining(size_t by) {
        backfillRemaining.fetch_add(by, std::memory_order_relaxed);
    }

    void markDiskSnapshot(uint64_t startSeqno, uint64_t endSeqno);

    bool backfillReceived(Item* itm, backfill_source_t backfill_source);

    void completeBackfill();

    bool isCompressionEnabled();

    void addStats(ADD_STAT add_stat, const void *c);

    void addTakeoverStats(ADD_STAT add_stat, const void *c);

    /* Returns a count of how many items are outstanding to be sent for this
     * stream's vBucket.
     */
    size_t getItemsRemaining();

    uint64_t getLastReadSeqno() const;

    uint64_t getLastSentSeqno() const;

    const Logger& getLogger() const;

    bool isSendMutationKeyOnlyEnabled() const;

    // Runs on ActiveStreamCheckpointProcessorTask
    void nextCheckpointItemTask();

    /* Function to handle a slow stream that is supposedly hogging memory in
       checkpoint mgr. Currently we handle the slow stream by switching from
       in-memory to backfilling */
    void handleSlowStream();

    /// @return a const reference to the streams cursor name
    const std::string& getCursorName() const {
        return cursorName;
    }

protected:
    // Returns the outstanding items for the stream's checkpoint cursor.
    virtual void getOutstandingItems(RCPtr<VBucket> &vb, std::vector<queued_item> &items);

    // Given a set of queued items, create mutation responses for each item,
    // and pass onto the producer associated with this stream.
    void processItems(std::vector<queued_item>& items);

    bool nextCheckpointItem();

    DcpResponse* nextQueuedItem();

    /* The transitionState function is protected (as opposed to private) for
     * testing purposes.
     */
    void transitionState(stream_state_t newState);

    /* Indicates that a backfill has been scheduled and has not yet completed.
     * Is protected (as opposed to private) for testing purposes.
     */
    std::atomic<bool> isBackfillTaskRunning;

    /* Indicates if another backfill must be scheduled following the completion
     * of current running backfill.  Guarded by streamMutex.
     * Is protected (as opposed to private) for testing purposes.
     */
    bool pendingBackfill;

private:

    DcpResponse* backfillPhase(LockHolder& lh);

    DcpResponse* inMemoryPhase();

    DcpResponse* takeoverSendPhase();

    DcpResponse* takeoverWaitPhase();

    DcpResponse* deadPhase();

    void snapshot(std::deque<MutationResponse*>& snapshot, bool mark);

    void endStream(end_stream_status_t reason);

    /* reschedule = FALSE ==> First backfill on the stream
     * reschedule = TRUE ==> Schedules another backfill on the stream that has
     *                       finished backfilling once and still in
     *                       STREAM_BACKFILLING state or in STREAM_IN_MEMORY
     *                       state.
     * Note: Expects the streamMutex to be acquired when called
     */
    void scheduleBackfill_UNLOCKED(bool reschedule);

    const char* getEndStreamStatusStr(end_stream_status_t status);

    bool isCurrentSnapshotCompleted() const;

    /* Drop the cursor registered with the checkpoint manager.
     * Note: Expects the streamMutex to be acquired when called
     */
    void dropCheckpointCursor_UNLOCKED();

    /* The last sequence number queued from disk or memory, but is yet to be
       snapshotted and put onto readyQ */
    AtomicValue<uint64_t> lastReadSeqnoUnSnapshotted;

    /* The last sequence number queued from disk or memory and is
       snapshotted and put onto readyQ */
    AtomicValue<uint64_t> lastReadSeqno;

    //! The last sequence number sent to the network layer
    AtomicValue<uint64_t> lastSentSeqno;

    //! The last known seqno pointed to by the checkpoint cursor
    AtomicValue<uint64_t> curChkSeqno;

    //! The current vbucket state to send in the takeover stream
    vbucket_state_t takeoverState;

    /* backfillRemaining is a stat recording the amount of
     * items remaining to be read from disk.  It is an atomic
     * because otherwise the function incrBackfillRemaining
     * must acquire the streamMutex lock.
     */
    AtomicValue <size_t> backfillRemaining;

    //! Stats to track items read and sent from the backfill phase
    struct {
        AtomicValue<size_t> memory;
        AtomicValue<size_t> disk;
        AtomicValue<size_t> sent;
    } backfillItems;

    //! The amount of items that have been sent during the memory phase
    AtomicValue<size_t> itemsFromMemoryPhase;

    //! Whether ot not this is the first snapshot marker sent
    bool firstMarkerSent;

    AtomicValue<int> waitForSnapshot;

    EventuallyPersistentEngine* engine;
    dcp_producer_t producer;

    struct {
        AtomicValue<uint32_t> bytes;
        AtomicValue<uint32_t> items;
    } bufferedBackfill;

    AtomicValue<rel_time_t> takeoverStart;
    size_t takeoverSendMaxTime;

    /* Enum indicating whether the stream mutations should contain key only or
       both key and value */
    MutationPayload payloadType;

    //! Last snapshot end seqno sent to the DCP client
    AtomicValue<uint64_t> lastSentSnapEndSeqno;

    /* Flag used by checkpointCreatorTask that is set before all items are
       extracted for given checkpoint cursor, and is unset after all retrieved
       items are added to the readyQ */
    AtomicValue<bool> chkptItemsExtractionInProgress;

    /**
     * The name which uniquely identifies this stream's checkpoint cursor
     */
    std::string cursorName;

    /**
     * To ensure each stream gets a unique cursorName, we maintain a 'uid'
     * which is really just an incrementing uint64
     */
    static std::atomic<uint64_t> cursorUID;

};


class ActiveStreamCheckpointProcessorTask : public GlobalTask {
public:
    ActiveStreamCheckpointProcessorTask(EventuallyPersistentEngine& e,
                                        dcp_producer_t p)
        : GlobalTask(&e, TaskId::ActiveStreamCheckpointProcessorTask,
                     INT_MAX, false),
      notified(false),
      iterationsBeforeYield(e.getConfiguration()
                            .getDcpProducerSnapshotMarkerYieldLimit()),
      producer(p) { }

    std::string getDescription() {
        std::string rv("Process checkpoint(s) for DCP producer");
        return rv;
    }

    bool run();
    void schedule(stream_t stream);
    void wakeup();

    /* Clears the queues and resets the producer reference */
    void cancelTask();

    /* Returns the number of unique streams waiting to be processed */
    size_t queueSize() {
        LockHolder lh(workQueueLock);
        return queue.size();
    }

private:

    stream_t queuePop() {
        stream_t rval;
        uint16_t vbid = 0;
        dcp_producer_t producerRefCpy;
        {
            LockHolder lh(workQueueLock);
            if (queue.empty()) {
                return rval;
            }
            vbid = queue.front();
            queue.pop();
            queuedVbuckets.erase(vbid);
            /* Get a copy of refPtr as we are releasing the workQueueLock */
            producerRefCpy = producer;
        }

        /* findStreamByVbid acquires DcpProducer::streamsMutex, hence called
           without acquiring workQueueLock */
        if (producerRefCpy) {
            return producerRefCpy->findStreamByVbid(vbid);
        }
        return rval;
    }

    bool queueEmpty() {
        LockHolder lh(workQueueLock);
        return queue.empty();
    }

    void pushUnique(uint16_t vbid) {
        LockHolder lh(workQueueLock);
        if (queuedVbuckets.count(vbid) == 0) {
            queue.push(vbid);
            queuedVbuckets.insert(vbid);
        }
    }

    Mutex workQueueLock;

    /**
     * Maintain a queue of unique vbucket ids for which stream should be
     * processed.
     * There's no need to have the same stream in the queue more than once
     *
     * The streams are kept in the 'streams map' of the producer object. We
     * should not hold a shared reference to the stream object here in order to
     * avoid multiple stream ownership issues
     */
    std::queue<uint16_t> queue;
    std::unordered_set<uint16_t> queuedVbuckets;

    AtomicValue<bool> notified;
    size_t iterationsBeforeYield;

    /* shared reference to the producer object, should be deleted when the task
       is stopped because the producer object contains a reference to this */
    dcp_producer_t producer;
};

class NotifierStream : public Stream {
public:
    NotifierStream(EventuallyPersistentEngine* e, dcp_producer_t producer,
                   const std::string &name, uint32_t flags, uint32_t opaque,
                   uint16_t vb, uint64_t start_seqno, uint64_t end_seqno,
                   uint64_t vb_uuid, uint64_t snap_start_seqno,
                   uint64_t snap_end_seqno);

    ~NotifierStream() {
        transitionState(STREAM_DEAD);
    }

    DcpResponse* next();

    uint32_t setDead(end_stream_status_t status);

    void notifySeqnoAvailable(uint64_t seqno);

private:

    void transitionState(stream_state_t newState);

    dcp_producer_t producer;
};

class PassiveStream : public Stream {
public:
    PassiveStream(EventuallyPersistentEngine* e, dcp_consumer_t consumer,
                  const std::string &name, uint32_t flags, uint32_t opaque,
                  uint16_t vb, uint64_t start_seqno, uint64_t end_seqno,
                  uint64_t vb_uuid, uint64_t snap_start_seqno,
                  uint64_t snap_end_seqno, uint64_t vb_high_seqno);

    ~PassiveStream();

    process_items_error_t processBufferedMessages(uint32_t &processed_bytes,
                                                  size_t batchSize);

    DcpResponse* next();

    uint32_t setDead(end_stream_status_t status);

    void acceptStream(uint16_t status, uint32_t add_opaque);

    void reconnectStream(RCPtr<VBucket> &vb, uint32_t new_opaque,
                         uint64_t start_seqno);

    ENGINE_ERROR_CODE messageReceived(DcpResponse* response);

    void addStats(ADD_STAT add_stat, const void *c);

private:

    ENGINE_ERROR_CODE processMutation(MutationResponse* mutation);

    ENGINE_ERROR_CODE processDeletion(MutationResponse* deletion);

    void handleSnapshotEnd(RCPtr<VBucket>& vb, uint64_t byseqno);

    void processMarker(SnapshotMarker* marker);

    void processSetVBucketState(SetVBucketState* state);

    bool transitionState(stream_state_t newState);

    uint32_t clearBuffer_UNLOCKED();

    const char* getEndStreamStatusStr(end_stream_status_t status);

    EventuallyPersistentEngine* engine;
    dcp_consumer_t consumer;

    AtomicValue<uint64_t> last_seqno;

    AtomicValue<uint64_t> cur_snapshot_start;
    AtomicValue<uint64_t> cur_snapshot_end;
    AtomicValue<snapshot_type_t> cur_snapshot_type;
    bool cur_snapshot_ack;

    struct Buffer {
        Buffer() : bytes(0), items(0) {}
        size_t bytes;
        size_t items;
        /* Lock ordering w.r.t to streamMutex:
           First acquire bufMutex and then streamMutex */
        Mutex bufMutex;
        std::queue<DcpResponse*> messages;
    } buffer;
};

#endif  // SRC_DCP_STREAM_H_
