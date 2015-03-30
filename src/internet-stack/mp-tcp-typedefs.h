#include <vector>
#include <map>
#include "sequence-number.h"
#include "rtt-estimator.h"
#include "tcp-typedefs.h"
#include "ns3/ipv4-address.h"
#include "ns3/event-id.h"
#include <stdint.h>
#include <queue>
#include <list>
#include <set>

#include "ns3/object.h"
#include "ns3/uinteger.h"
#include "ns3/traced-value.h"
#include "ns3/trace-source-accessor.h"

#ifndef MP_TCP_TYPEDEFS_H
#define MP_TCP_TYPEDEFS_H

using namespace std;

namespace ns3 {

typedef enum {
  MP_NONE,        // 0
  MP_MPC,         // 1
  MP_ADDR,        // 2
  MP_JOIN} MpStates_t;
// Multipath actions

// congestion control algorithm
typedef enum {
  Uncoupled_TCPs,       // 0
  Linked_Increases,     // 1
  RTT_Compensator,      // 2
  Fully_Coupled         // 3
  } CongestionCtrl_t;

// connection phase
typedef enum {
  Slow_Start,                   // 0
  Congestion_Avoidance,         // 1
  DSACK_SS,                     // 2 DSACK Slow Start: a temporary slow start triggered after detecting spurious retransmission based on DSACK information
  RTO_Recovery                  // 3 Reconvery algorithm after RTO expiration
  } Phase_t;

typedef enum {
  Round_Robin        // 0
  //Collision_Avoidance         // 1
  } DataDistribAlgo_t;

typedef enum {
  NoPR_Algo,    // 0
  Eifel,        // 1
  TCP_DOOR,     // 2 Detection of Out-of-Order and Response
  D_SACK,       // 3 Duplicate SACK (Selective ACKnowledgement)
  F_RTO         // 4 Forward RTO-Recovery: Algorithm for detecting spurious retransmission timeouts
  } PacketReorder_t;

typedef enum {
  Step_1,       // 0
  Step_2,       // 1
  Step_3,       // 2
  Step_4        // 3 In this step of F-RTO do a standard Fast Recovery algorithm
  } FRtoStep_t;

class DSNMapping
{
public:
    DSNMapping ();
    DSNMapping (uint8_t sFlowIdx, uint64_t dSeqNum, uint16_t dLvlLen, uint32_t sflowSeqNum, uint32_t ack, Ptr<Packet> pkt);
    //DSNMapping (const DSNMapping &res);
    virtual ~DSNMapping();
    uint64_t dataSeqNumber;
    uint16_t dataLevelLength;
    uint32_t subflowSeqNumber;
    uint32_t acknowledgement;
    uint32_t dupAckCount;
    uint8_t  subflowIndex;
    uint8_t *packet;

    bool operator <  (const DSNMapping& rhs) const;

    // variables for reordering simulation
    // Eifel Algorithm
    bool     retransmited;
    uint64_t tsval; // TimesTamp value

/*
private:/
    bool original;
    */
};

typedef enum {
  NO_ACTION,       // 0
  ADDR_TX,
  INIT_SUBFLOWS} MpActions_t;

class MpTcpStateMachine : public TcpStateMachine
{
public:

    MpTcpStateMachine();
    virtual ~MpTcpStateMachine();

    string printState(States_t s);
    string printEvent(Events_t e);
    string printAction(Actions_t a);

};

class MpTcpSubFlow : public Object
{
public:
    static TypeId GetTypeId (void);

    MpTcpSubFlow ();
    ~MpTcpSubFlow ();
    MpTcpSubFlow (uint32_t TxSeqNb);

    void StartTracing (string traced);
    void CwndTracer (double oldval, double newval);

    void AddDSNMapping(uint8_t sFlowIdx, uint64_t dSeqNum, uint16_t dLvlLen, uint32_t sflowSeqNum, uint32_t ack, Ptr<Packet> pkt);
    void updateRTT (uint32_t ack, Time current);
    DSNMapping * GetunAckPkt (uint32_t awnd);

    uint16_t    routeId;
    bool        connected;
    States_t    state;
    Phase_t     phase;
    Ipv4Address sAddr;
    uint16_t    sPort;
    Ipv4Address dAddr;
    uint16_t    dPort;
    uint32_t    oif;

    EventId  retxEvent;
    uint32_t MSS;          // Maximum Segment Size
    //double   cwnd;
    TracedValue<double> cwnd;
    double   scwnd;         // smoothed congestion window
    uint32_t ssthresh;
    uint32_t maxSeqNb;     // it represent the highest sequence number of a sent byte. In general it's egual to ( TxSeqNumber - 1 ) until a retransmission happen
    uint32_t highestAck;   // hightest received ACK for the subflow level sequence number
    uint64_t bandwidth;

    list<DSNMapping *> mapDSN;
    multiset<double> measuredRTT;
    //list<double> measuredRTT;
    Ptr<RttMeanDeviation> rtt;
    Time     lastMeasuredRtt;
    uint32_t TxSeqNumber;
    uint32_t RxSeqNumber;

    // for losses simulation
    double   LostThreshold;
    bool     CanDrop;
    uint64_t PktCount;
    uint64_t MaxPktCount;
    uint32_t DropedPktCount;
    uint32_t MaxDropedPktCount;

    // Reordering simulation
    double   savedCWND;
    uint32_t savedSSThresh;
    bool     SpuriousRecovery;
    uint32_t recover;
    uint8_t  ackCount;      // count of received acknowledgement after an RTO expiration
    uint32_t ReTxSeqNumber; // higher sequence number of the retransmitted segment
    int      nbRecvAck;
};

class MpTcpAddressInfo
{
public:
    MpTcpAddressInfo();
    ~MpTcpAddressInfo();

    uint8_t     addrID;
    Ipv4Address ipv4Addr;
    Ipv4Mask    mask;
};

class DataBuffer
{
public:
    DataBuffer();
    DataBuffer(uint32_t size);
    ~DataBuffer();

    queue<uint8_t> buffer;
    uint32_t bufMaxSize;

    uint32_t Add(uint8_t* buf, uint32_t size);
    uint32_t Retrieve(uint8_t* buf, uint32_t size);
    Ptr<Packet> CreatePacket (uint32_t size);
    uint32_t ReadPacket (Ptr<Packet> pkt, uint32_t dataLen);
    bool     Empty();
    bool     Full ();
    uint32_t PendingData();
    uint32_t FreeSpaceSize();
};


}//namespace ns3
#endif //MP_TCP_TYPEDEFS_H
