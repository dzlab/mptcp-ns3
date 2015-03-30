#include "ns3/abort.h"
#include "ns3/node.h"
#include "ns3/inet-socket-address.h"
#include "ns3/log.h"
#include "ns3/ipv4.h"
#include "ns3/ipv4-interface-address.h"
#include "ns3/ipv4-route.h"
#include "ns3/ipv4-interface.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/simulation-singleton.h"
#include "ns3/simulator.h"
#include "ns3/packet.h"
#include "ns3/uinteger.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/string.h"
#include "tcp-typedefs.h"
#include "mp-tcp-typedefs.h"
#include "tcp-socket-impl.h"
#include "mp-tcp-socket-impl.h"
#include "mp-tcp-l4-protocol.h"
#include "ipv4-end-point.h"
#include "tcp-header.h"
#include "rtt-estimator.h"
#include "ipv4-l3-protocol.h"
#include "ns3/gnuplot.h"
#include "ns3/error-model.h"
#include "time.h"
#include "ns3/point-to-point-channel.h"
#include "ns3/point-to-point-net-device.h"

#include <vector>
#include <map>
#include <algorithm>
#include <stdlib.h>

#include <iostream>
#include <fstream>

NS_LOG_COMPONENT_DEFINE ("MpTcpSocketImpl");

using namespace std;

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (MpTcpSocketImpl);

TypeId
MpTcpSocketImpl::GetTypeId (void)
{
    static TypeId tid = TypeId("ns3::MpTcpSocketImpl").SetParent<TcpSocketImpl> ();
    return tid;
}

MpTcpSocketImpl::MpTcpSocketImpl ()
    //: subflows (10)//, senderAddrs (10), receiverAddrs (10)
{
    NS_LOG_FUNCTION(this);
    //srand (time(NULL));
    srand (65536);
    m_state        = CLOSED;
    m_node         = 0;
    m_connected    = false;
    m_stateMachine = new MpTcpStateMachine();

    // multipath variable
    MaxSubFlowNumber = 255;
    MinSubFlowNumber = 1;
    SubFlowNumber = 0;
    mpState     = MP_NONE;
    mpSendState = MP_NONE;
    mpRecvState = MP_NONE;
    mpEnabled   = false;
    addrAdvertised = false;
    localToken  = 0;
    remoteToken = 0;
    aggregatedBandwidth = 0;
    lastUsedsFlowIdx = 0;

    totalCwnd = 0;
    meanTotalCwnd = 0;

    // flow control
    m_rxBufSize  = 0;
    m_lastRxAck  = 0;

    m_skipRetxResched = false;
    m_dupAckCount  = 0;
    m_delAckCount  = 0;

    nextTxSequence   = 1;
    nextRxSequence   = 1;
    remoteRecvWnd    = 1;
    unAckedDataCount = 0;

    AlgoCC = RTT_Compensator;
    distribAlgo = Round_Robin;
    AlgoPR = NoPR_Algo;

    nbRejected = 0;
    nbReceived = 0;
    unOrdMaxSize  = 0;

    client = false;
    server = false;

    lastRetransmit = 0;
    frtoStep = Step_1;
    useFastRecovery = false;
}

MpTcpSocketImpl::MpTcpSocketImpl (Ptr<Node> node)
    : subflows (0), localAddrs (0), remoteAddrs (0)
{
    NS_LOG_FUNCTION(node->GetId() << this);
    //srand (time(NULL));
    srand (65536);
    m_state        = CLOSED;
    m_node         = node;
    m_connected    = false;
    m_stateMachine = new MpTcpStateMachine();
    m_mptcp = node->GetObject<MpTcpL4Protocol> ();
    NS_ASSERT_MSG(m_mptcp != 0, "node->GetObject<MpTcpL4Protocol> () returned NULL");

    // multipath variable
    MaxSubFlowNumber = 255;
    MinSubFlowNumber = 1;
    SubFlowNumber    = 0;
    mpState = MP_NONE;
    mpSendState = MP_NONE;
    mpRecvState = MP_NONE;
    mpEnabled = false;
    addrAdvertised = false;
    localToken = 0;
    remoteToken = 0;
    aggregatedBandwidth = 0;
    lastUsedsFlowIdx = 0;

    totalCwnd = 0;
    meanTotalCwnd = 0;

    nextTxSequence   = 1;
    nextRxSequence   = 1;

    m_skipRetxResched = false;
    m_dupAckCount = 0;
    m_delAckCount = 0;

    // flow control
    m_rxBufSize = 0;
    m_lastRxAck = 0;
    congestionWnd = 1;

    remoteRecvWnd = 1;
    unAckedDataCount = 0;

    AlgoCC = RTT_Compensator;
    distribAlgo = Round_Robin;
    AlgoPR = NoPR_Algo;

    nbRejected = 0;
    nbReceived = 0;
    unOrdMaxSize  = 0;

    client = false;
    server = false;

    lastRetransmit = 0;
    frtoStep = Step_1;
    useFastRecovery = false;
}

MpTcpSocketImpl::~MpTcpSocketImpl ()
{
    NS_LOG_FUNCTION (m_node->GetId() << this);
    m_state        = CLOSED;
    m_node         = 0;
    m_mptcp        = 0;
    m_connected    = false;
    delete m_stateMachine;
    delete m_pendingData;
    //subflows.clear();
    for(int i = 0; i < (int) localAddrs.size(); i ++)
        delete localAddrs[i];
    localAddrs.clear();
    for(int i = 0; i < (int) remoteAddrs.size(); i ++)
        delete remoteAddrs[i];
    remoteAddrs.clear();
    delete sendingBuffer;
    delete recvingBuffer;
}

uint8_t
MpTcpSocketImpl::GetMaxSubFlowNumber ()
{
    return MaxSubFlowNumber;
}

void
MpTcpSocketImpl::SetMaxSubFlowNumber (uint8_t num)
{
    MaxSubFlowNumber = num;
}

uint8_t
MpTcpSocketImpl::GetMinSubFlowNumber ()
{
    return MinSubFlowNumber;
}

void
MpTcpSocketImpl::SetMinSubFlowNumber (uint8_t num)
{
    MinSubFlowNumber = num;
}

bool
MpTcpSocketImpl::SetLossThreshold(uint8_t sFlowIdx, double lossThreshold)
{
    if(sFlowIdx >= subflows.size())
        return false;
    subflows[sFlowIdx]->LostThreshold = lossThreshold;
    return true;
}

void
MpTcpSocketImpl::SetPacketReorderAlgo (PacketReorder_t pralgo)
{
    AlgoPR = pralgo;
}

Ptr<MpTcpSocketImpl>
MpTcpSocketImpl::Copy ()
{
  return CopyObject<MpTcpSocketImpl> (this);
}

void
MpTcpSocketImpl::SetNode (Ptr<Node> node)
{
    m_node = node;
}

Ptr<Node>
MpTcpSocketImpl::GetNode ()
{
    return m_node;
}

void
MpTcpSocketImpl::SetMpTcp (Ptr<MpTcpL4Protocol> mptcp)
{
    m_mptcp = mptcp;
}

uint32_t
MpTcpSocketImpl::getL3MTU (Ipv4Address addr)
{
    // return the MTU associated to the layer 3
    Ptr<Ipv4L3Protocol> l3Protocol = m_node->GetObject<Ipv4L3Protocol> ();
    return l3Protocol->GetMtu ( l3Protocol->GetInterfaceForAddress (addr) )-100;
}

uint64_t
MpTcpSocketImpl::getBandwidth (Ipv4Address addr)
{
    uint64_t bd = 0;
    StringValue uiv;
    std::string name = std::string("DataRate");
    Ptr<Ipv4L3Protocol> l3Protocol = m_node->GetObject<Ipv4L3Protocol> ();
    Ptr<Ipv4Interface> ipv4If = l3Protocol->GetInterface (l3Protocol->GetInterfaceForAddress (addr));
    Ptr< NetDevice > netDevice = ipv4If->GetDevice();
    // if the device is a point to point one, then the data rate can be retrived directly from the device
    // if it's a CSMA one, then you should look at the corresponding channel
    if( netDevice->IsPointToPoint () == true )
    {
        netDevice->GetAttribute(name, (AttributeValue &) uiv);
        // converting the StringValue to a string, then deleting the 'bps' end
        std::string str = uiv.SerializeToString(0);
        std::istringstream iss( str.erase(str.size() - 3) );
        iss >> bd;
    }
    return bd;
}

int
MpTcpSocketImpl::Listen (void)
{
    //NS_LOG_FUNCTION_NOARGS();
    // detect all interfaces on which the node can receive a SYN packet

    MpTcpSubFlow *sFlow = new MpTcpSubFlow();
    sFlow->routeId = (subflows.size() == 0 ? 0:subflows[subflows.size() - 1]->routeId + 1);
    sFlow->sAddr   = m_endPoint->GetLocalAddress ();
    sFlow->sPort   = m_endPoint->GetLocalPort ();
    m_localPort    = m_endPoint->GetLocalPort ();
    sFlow->MSS     = getL3MTU (m_endPoint->GetLocalAddress ());
    sFlow->bandwidth = getBandwidth (m_endPoint->GetLocalAddress ());
    subflows.insert (subflows.end(), sFlow);

    if(m_state != CLOSED)
    {
        m_errno = ERROR_INVAL;
        return -1;
    }
    // aT[CLOSED][APP_LISTEN] = SA(LISTEN, NO_ACT)
    // used as a reference when creating subsequent subflows
    m_state = LISTEN;

    ProcessAction(subflows.size() - 1 , ProcessEvent(subflows.size() - 1, APP_LISTEN));
    return 0;
}

int
MpTcpSocketImpl::Connect (Ipv4Address servAddr, uint16_t servPort)
{
    //NS_LOG_FUNCTION (m_node->GetId()<< this << servAddr << servPort );
    MpTcpSubFlow *sFlow = new MpTcpSubFlow();
    sFlow->routeId  = (subflows.size() == 0 ? 0:subflows[subflows.size() - 1]->routeId + 1);
    sFlow->dAddr    = servAddr;
    sFlow->dPort    = servPort;
    m_remoteAddress = servAddr;
    m_remotePort    = servPort;
    Ptr<Ipv4> ipv4  = m_node->GetObject<Ipv4> ();
    if (m_endPoint == 0)
    {// no end point allocated for this socket => try to allocate a new one
        if (Bind() == -1)
        {
            NS_ASSERT (m_endPoint == 0);
            return -1;
        }
        NS_ASSERT (m_endPoint != 0);
    }
    // check if whether the node have a routing protocol
    sFlow->sAddr = m_endPoint->GetLocalAddress ();
    sFlow->sPort = m_endPoint->GetLocalPort ();
    sFlow->MSS   = getL3MTU(m_endPoint->GetLocalAddress ());
    sFlow->bandwidth = getBandwidth(m_endPoint->GetLocalAddress ());
    subflows.insert(subflows.end(), sFlow);

    if(ipv4->GetRoutingProtocol() == 0)
    {
        NS_FATAL_ERROR("No Ipv4RoutingProtocol in the node");
    }
    else
    {
        Ipv4Header header;
        header.SetDestination (servAddr);
        Socket::SocketErrno errno_;
        Ptr<Ipv4Route> route;
        uint32_t oif = 0;
        route = ipv4->GetRoutingProtocol ()->RouteOutput(Ptr<Packet> (), header, oif, errno_);
        if (route != 0)
        {
            NS_LOG_LOGIC("Route existe");
            m_endPoint->SetLocalAddress (route->GetSource ());
        }
        else
        {
            NS_LOG_LOGIC ("MpTcpSocketImpl"<<m_node->GetId()<<"::Connect():  Route to " << m_remoteAddress << " does not exist");
            NS_LOG_ERROR (errno_);
            m_errno = errno_;
            return -1;
        }
    }
    // Sending SYN packet
    bool success = ProcessAction (subflows.size() - 1, ProcessEvent (subflows.size() - 1,APP_CONNECT) );
    if ( !success )
    {
        return -1;
    }
  return 0;
}

int
MpTcpSocketImpl::Connect (Address &address)
{
    //NS_LOG_FUNCTION ( this << address );

    // convert the address (Address => InetSocketAddress)
    InetSocketAddress transport = InetSocketAddress::ConvertFrom (address);
    m_remoteAddress = transport.GetIpv4();
    m_remotePort    = transport.GetPort();
    return Connect(m_remoteAddress, m_remotePort);
}

int
MpTcpSocketImpl::Bind ()
{
    //Allocate an endPoint for this socket
    //NS_LOG_FUNCTION_NOARGS ();
    client = true;
    m_endPoint = m_mptcp->Allocate();
    return Binding();
}

int
MpTcpSocketImpl::Binding (void)
{
    //NS_LOG_FUNCTION_NOARGS ();
    if (m_endPoint == 0)
    {
        return -1;
    }
    // set the call backs method
    m_endPoint->SetRxCallback (MakeCallback (&MpTcpSocketImpl::ForwardUp, Ptr<MpTcpSocketImpl>(this)));
    m_endPoint->SetDestroyCallback (MakeCallback (&MpTcpSocketImpl::Destroy, Ptr<MpTcpSocketImpl>(this)));

    // set the local parameters
    m_localAddress = m_endPoint->GetLocalAddress();
    m_localPort    = m_endPoint->GetLocalPort();
    return 0;
}

int
MpTcpSocketImpl::Bind (const Address &address)
{
  //NS_LOG_FUNCTION (m_node->GetId()<<":"<<this<<address);
  server = true;
  if (!InetSocketAddress::IsMatchingType (address))
    {
      m_errno = ERROR_INVAL;
      return -1;
    }
  else
      NS_LOG_DEBUG("MpTcpSocketImpl:Bind: Address ( " << address << " ) is valid");
  InetSocketAddress transport = InetSocketAddress::ConvertFrom (address);
  Ipv4Address ipv4 = transport.GetIpv4 ();
  uint16_t port = transport.GetPort ();
  NS_LOG_LOGIC("MpTcpSocketImpl:Bind: Ipv4Address: "<< ipv4 << ", Port: " << port);

  if (ipv4 == Ipv4Address::GetAny () && port == 0)
    {
      m_endPoint = m_mptcp->Allocate ();
      NS_LOG_LOGIC ("MpTcpSocketImpl "<<this<<" got an endpoint: "<<m_endPoint);
    }
  else if (ipv4 == Ipv4Address::GetAny () && port != 0)
    {
      m_endPoint = m_mptcp->Allocate (port);
      NS_LOG_LOGIC ("MpTcpSocketImpl "<<this<<" got an endpoint: "<<m_endPoint);
    }
  else if (ipv4 != Ipv4Address::GetAny () && port == 0)
    {
      m_endPoint = m_mptcp->Allocate (ipv4);
      NS_LOG_LOGIC ("MpTcpSocketImpl "<<this<<" got an endpoint: "<<m_endPoint);
    }
  else if (ipv4 != Ipv4Address::GetAny () && port != 0)
    {
      m_endPoint = m_mptcp->Allocate (ipv4, port);
      NS_LOG_LOGIC ("MpTcpSocketImpl "<<this<<" got an endpoint: "<<m_endPoint);
    }else
        NS_LOG_DEBUG("MpTcpSocketImpl:Bind(@): unable to allocate an end point !");

  return Binding ();
}

bool
MpTcpSocketImpl::SendBufferedData ()
{
  //NS_LOG_FUNCTION_NOARGS();
  uint8_t sFlowIdx = lastUsedsFlowIdx; // i prefer getSubflowToUse (), but this one gives the next one
  Ptr<Ipv4L3Protocol> ipv4 = m_node->GetObject<Ipv4L3Protocol> ();

  if ( !ProcessAction (sFlowIdx, ProcessEvent (sFlowIdx, APP_SEND) ))
  {
      return false; // Failed, return zero
  }
  return true;
}

int
MpTcpSocketImpl::FillBuffer (uint8_t* buf, uint32_t size)
{
  //NS_LOG_FUNCTION( this << size );
  return sendingBuffer->Add(buf, size);
}

bool
MpTcpSocketImpl::SendPendingData ()
{
    NS_LOG_INFO("====================================================================================");
  //NS_LOG_FUNCTION_NOARGS ();
  uint32_t nOctetsSent = 0;

  MpTcpSubFlow *sFlow;

  while ( ! sendingBuffer->Empty() )
  {
        uint8_t count   = 0;
        uint32_t window = 0;
        while ( count < subflows.size() )
        {
            count ++;
            window = std::min (AvailableWindow (lastUsedsFlowIdx), sendingBuffer->PendingData()); // Get available window size
            if( window == 0 )
            {
                // No more available window in the current subflow, try with another one
                lastUsedsFlowIdx = getSubflowToUse ();
            }
            else
            {
                NS_LOG_LOGIC ("MpTcpSocketImpl::SendPendingData -> PendingData (" << sendingBuffer->PendingData() << ") Available window ("<<AvailableWindow (lastUsedsFlowIdx)<<")");
                break;
            }
        }
        if ( count == subflows.size() && window == 0 )
        {
            // No available window for transmission in all subflows, abort sending
            break;
        }

      sFlow = subflows[lastUsedsFlowIdx];
      if(sFlow->state == ESTABLISHED)
      {
        Ipv4Address sAddr   = sFlow->sAddr;
        Ipv4Address dAddr   = sFlow->dAddr;
        uint16_t sPort      = sFlow->sPort;
        uint16_t dPort      = sFlow->dPort;
        uint32_t mss        = sFlow->MSS;
        uint8_t hlen = 5;
        uint8_t olen = 15 ;
        uint8_t plen = 0;

        uint32_t size = std::min (window, mss);  // Send no more than window
        Ptr<Packet> pkt = sendingBuffer->CreatePacket(size);
        if(pkt == 0)
            break;

        NS_LOG_LOGIC ("MpTcpSocketImpl SendPendingData on subflow " << (int)lastUsedsFlowIdx << " w " << window << " rxwin " << AdvertisedWindowSize () << " CWND "  << sFlow->cwnd << " segsize " << sFlow->MSS << " nextTxSeq " << nextTxSequence << " nextRxSeq " << nextRxSequence << " pktSize " << size);
        uint8_t  flags   = TcpHeader::ACK;

        MpTcpHeader header;
        header.SetSourcePort      (sPort);
        header.SetDestinationPort (dPort);
        header.SetFlags           (flags);
        header.SetSequenceNumber  (sFlow->TxSeqNumber);
        header.SetAckNumber       (sFlow->RxSeqNumber);
        header.SetWindowSize      (AdvertisedWindowSize());
      // save the seq number of the sent data
        sFlow->AddDSNMapping      (lastUsedsFlowIdx, nextTxSequence, size, sFlow->TxSeqNumber, sFlow->RxSeqNumber, pkt->Copy() );
        header.AddOptDSN          (OPT_DSN, nextTxSequence, size, sFlow->TxSeqNumber);

        switch ( AlgoPR )
        {
            case Eifel:
                header.AddOptTT   (OPT_TT, Simulator::Now ().GetMilliSeconds (), 0);
                olen += 17;
                break;
            default:
                break;
        }

        plen = (4 - (olen % 4)) % 4;
        olen = (olen + plen) / 4;
        hlen += olen;
        header.SetLength(hlen);
        header.SetOptionsLength(olen);
        header.SetPaddingLength(plen);

        SetReTxTimeout (lastUsedsFlowIdx);

        // simulating loss of acknowledgement in the sender side
        calculateTotalCWND ();


          if( sFlow->LostThreshold > 0.0 && sFlow->LostThreshold < 1.0 )
          {
              //Ptr<RateErrorModel> eModel = CreateObjectWithAttributes<RateErrorModel> ("RanVar", RandomVariableValue (UniformVariable (0., 1.)), "ErrorRate", DoubleValue (sFlow->LostThreshold));
              //if ( ! eModel->IsCorrupt (pkt) )
              if ( rejectPacket(sFlow->LostThreshold) == false )
              {
                 m_mptcp->SendPacket (pkt, header, sAddr, dAddr);
              }else
              {
                  NS_LOG_WARN("sFlowIdx "<<(int) lastUsedsFlowIdx<<" -> Packet Droped !");
              }
          }else
          {
              m_mptcp->SendPacket (pkt, header, sAddr, dAddr);
          }

        NS_LOG_WARN (Simulator::Now().GetSeconds() <<" SentSegment -> "<< " localToken "<< localToken<<" Subflow "<<(int) lastUsedsFlowIdx<<" DataTxSeq "<<nextTxSequence<<" SubflowTxSeq "<<sFlow->TxSeqNumber<<" SubflowRxSeq "<< sFlow->RxSeqNumber <<" Data "<< size <<" unAcked Data " << unAckedDataCount <<" octets" );

        // Notify the application of the data being sent
        sFlow->rtt->SentSeq (sFlow->TxSeqNumber, size);           // notify the RTT
        nOctetsSent        += size;                               // Count sent this loop
        nextTxSequence     += size;                // Advance next tx sequence
        sFlow->TxSeqNumber += size;
        sFlow->maxSeqNb    += size;
        unAckedDataCount   += size;
      }
      lastUsedsFlowIdx = getSubflowToUse ();
  }
  NS_LOG_LOGIC ("RETURN SendPendingData -> amount data sent = " << nOctetsSent);
  NotifyDataSent( GetTxAvailable() );

  return ( nOctetsSent>0 );
}

uint8_t
MpTcpSocketImpl::getSubflowToUse ()
{
    uint8_t nextSubFlow = 0;
    switch ( distribAlgo )
    {
        case Round_Robin :
            nextSubFlow = (lastUsedsFlowIdx + 1) % subflows.size();
            break;
        default:
            break;
    }
    return nextSubFlow;
}

void
MpTcpSocketImpl::ReTxTimeout (uint8_t sFlowIdx)
{ // Retransmit timeout
  //NS_LOG_INFO("++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++");
  //NS_LOG_FUNCTION (this);
  MpTcpSubFlow *sFlow = subflows[sFlowIdx];
  NS_LOG_WARN ("Subflow ("<<(int)sFlowIdx<<") ReTxTimeout Expired at time "<<Simulator::Now ().GetSeconds()<< " unacked packets count is "<<sFlow->mapDSN.size() );

  // If erroneous timeout in closed/timed-wait state, just return
  if (m_state == CLOSED || m_state == TIMED_WAIT)
  {
      NS_LOG_WARN ("ReTxTimeout subflow ("<<(int)sFlowIdx<<") current state is "<<m_stateMachine->printState(m_state));
      return;
  }
  reduceCWND (sFlowIdx);
  // Set cWnd to segSize on timeout,  per rfc2581
  // Collapse congestion window (re-enter slowstart)

  Retransmit (sFlowIdx);             // Retransmit the packet
  if( AlgoPR == F_RTO )
  {
      sFlow->SpuriousRecovery = false;
      if( (sFlow->phase == RTO_Recovery) && (sFlow->recover >= sFlow->highestAck + 1) )
      {
          sFlow->recover  = sFlow->TxSeqNumber; // highest sequence number transmitted
          sFlow->ackCount = 0;
          frtoStep = Step_4;    // go to step 4 to perform the standard Fast Recovery algorithm
      }else
      {
          frtoStep = Step_2;    // enter step 2 of the F-RTO algorithm
          NS_LOG_WARN("Entering step 2 of the F-RTO algorithm");
      }
      sFlow->phase = RTO_Recovery; // in RTO recovery algorithm, sender do slow start retransmissions
  }
}

void
MpTcpSocketImpl::reduceCWND (uint8_t sFlowIdx)
{
    MpTcpSubFlow *sFlow = subflows[sFlowIdx];
    double cwnd = sFlow->cwnd;
    calculateTotalCWND ();

    // save current congestion state
    switch ( AlgoPR )
    {
        case D_SACK:
            sFlow->savedCWND = std::max (cwnd, sFlow->savedCWND);
            sFlow->savedSSThresh = std::max(sFlow->ssthresh, sFlow->savedSSThresh);
            break;
        default:
            sFlow->savedCWND = cwnd;
            sFlow->savedSSThresh = sFlow->ssthresh;
            break;
    }


    sFlow->ssthresh = (std::min(remoteRecvWnd, static_cast<uint32_t>(sFlow->cwnd))) / 2; // Per RFC2581
    sFlow->ssthresh = std::max (sFlow->ssthresh, 2 * sFlow->MSS);

    double gThroughput = getGlobalThroughput();
    uint64_t lDelay = getPathDelay(sFlowIdx);

    switch ( AlgoCC )
    {
        case Uncoupled_TCPs:
            sFlow->cwnd  = std::max (cwnd  / 2, 1.0);
            NS_LOG_WARN (Simulator::Now().GetSeconds() <<" MpTcpSocketImpl -> "<< " localToken "<< localToken<<" Subflow "<< (int)sFlowIdx <<": RTT "<< sFlow->rtt->est.GetSeconds() <<" reducing cwnd from " << cwnd << " to "<<sFlow->cwnd <<" Throughput "<< (sFlow->cwnd * sFlow->MSS * 8) / sFlow->rtt->est.GetSeconds() << " GlobalThroughput "<<gThroughput<< " Efficacity " <<  getConnectionEfficiency() << " delay "<<lDelay << " Uncoupled_TCPs" );
            break;
        case Linked_Increases:
            sFlow->cwnd  = std::max (cwnd  / 2, 1.0);
            NS_LOG_WARN (Simulator::Now().GetSeconds() <<" MpTcpSocketImpl -> "<< " localToken "<< localToken<<" Subflow "<< (int)sFlowIdx <<": RTT "<< sFlow->rtt->est.GetSeconds() <<" reducing cwnd from " << cwnd << " to "<<sFlow->cwnd <<" Throughput "<< (sFlow->cwnd * sFlow->MSS * 8) / sFlow->rtt->est.GetSeconds() << " GlobalThroughput "<<gThroughput<< " Efficacity " <<  getConnectionEfficiency() << " delay "<<lDelay <<" alpha "<< alpha << " Linked_Increases");
            break;
        case RTT_Compensator:
            sFlow->cwnd  = std::max (cwnd  / 2, 1.0);
            NS_LOG_WARN (Simulator::Now().GetSeconds() <<" MpTcpSocketImpl -> "<< " localToken "<< localToken<<" Subflow "<< (int)sFlowIdx <<": RTT "<< sFlow->rtt->est.GetSeconds() <<" reducing cwnd from " << cwnd << " to "<<sFlow->cwnd <<" Throughput "<< (sFlow->cwnd * sFlow->MSS * 8) / sFlow->rtt->est.GetSeconds() << " GlobalThroughput "<<gThroughput<< " Efficacity " <<  getConnectionEfficiency() << " delay "<<lDelay <<" alpha "<< alpha << " RTT_Compensator");
            break;
        case Fully_Coupled:
            sFlow->cwnd  = std::max (cwnd - totalCwnd / 2, 1.0);
            NS_LOG_WARN (Simulator::Now().GetSeconds() <<" MpTcpSocketImpl -> "<< " localToken "<< localToken<<" Subflow "<< (int)sFlowIdx <<": RTT "<< sFlow->rtt->est.GetSeconds() <<" reducing cwnd from " << cwnd << " to "<<sFlow->cwnd <<" Throughput "<< (sFlow->cwnd * sFlow->MSS * 8) / sFlow->rtt->est.GetSeconds() << " GlobalThroughput "<<gThroughput<< " Efficacity " <<  getConnectionEfficiency() << " delay "<<lDelay <<" alpha "<< alpha << " Fully_Coupled");
            break;
        default:
            sFlow->cwnd  = 1;
            NS_LOG_WARN (Simulator::Now().GetSeconds() <<" MpTcpSocketImpl -> "<< " localToken "<< localToken<<" Subflow "<< (int)sFlowIdx <<": RTT "<< sFlow->rtt->est.GetSeconds() <<" reducing cwnd from " << cwnd << " to "<<sFlow->cwnd <<" Throughput "<< (sFlow->cwnd * sFlow->MSS * 8) / sFlow->rtt->est.GetSeconds() << " GlobalThroughput "<<gThroughput<< " Efficacity " <<  getConnectionEfficiency() << " delay "<<lDelay << " default");
            break;
    }

    sFlow->phase = Congestion_Avoidance;
    // sFlow->TxSeqNumber = sFlow->highestAck + 1; // Start from highest Ack
    sFlow->rtt->IncreaseMultiplier (); // DoubleValue timeout value for next retx timer
}

void
MpTcpSocketImpl::Retransmit (uint8_t sFlowIdx)
{
  //NS_LOG_FUNCTION (this);
  MpTcpSubFlow *sFlow = subflows[sFlowIdx];
  uint8_t flags = TcpHeader::ACK;
  uint8_t hlen = 5;
  uint8_t olen = 15;
  uint8_t plen = 0;

  //NS_ASSERT(sFlow->TxSeqNumber == sFlow->RxSeqNumber);
  DSNMapping * ptrDSN = sFlow->GetunAckPkt (AvailableWindow (sFlowIdx));
  if (ptrDSN == 0)
  {
      NS_LOG_WARN ("Retransmit -> no Unacked data !! mapDSN size is "<< sFlow->mapDSN.size() << " max Ack seq n° "<< sFlow->highestAck);
      return;
  }
  // Calculate remaining data for COE check
  Ptr<Packet> pkt = new Packet (ptrDSN->packet, ptrDSN->dataLevelLength);

  NS_LOG_WARN (Simulator::Now().GetSeconds() <<" RetransmitSegment -> "<< " localToken "<< localToken<<" Subflow "<<(int) sFlowIdx<<" DataSeq "<< ptrDSN->dataSeqNumber <<" SubflowSeq " << ptrDSN->subflowSeqNumber <<" dataLength " << ptrDSN->dataLevelLength << " packet size " << pkt->GetSize() <<" RTO_Timeout" );

  SetReTxTimeout (sFlowIdx);

  //sFlow->rtt->SentSeq (ptrDSN->subflowSeqNumber, ptrDSN->dataLevelLength);
  sFlow->rtt->pktRetransmit (ptrDSN->subflowSeqNumber);

  // And send the packet
  MpTcpHeader mptcpHeader;
  mptcpHeader.SetSequenceNumber  (ptrDSN->subflowSeqNumber);
  mptcpHeader.SetAckNumber       (sFlow->RxSeqNumber);
  mptcpHeader.SetSourcePort      (sFlow->sPort);
  mptcpHeader.SetDestinationPort (sFlow->dPort);
  mptcpHeader.SetFlags           (flags);
  mptcpHeader.SetWindowSize      (AdvertisedWindowSize());

    mptcpHeader.AddOptDSN (OPT_DSN, ptrDSN->dataSeqNumber, ptrDSN->dataLevelLength, ptrDSN->subflowSeqNumber);

    switch ( AlgoPR )
    {
        case Eifel:
            if(ptrDSN->retransmited == false)
            {
                ptrDSN->retransmited = true;
                ptrDSN->tsval = Simulator::Now ().GetMilliSeconds (); // update timestamp value to the current time
            }
            mptcpHeader.AddOptTT  (OPT_TT, ptrDSN->tsval, 0);
            olen += 17;
            break;
        case D_SACK:
            if(ptrDSN->retransmited == false)
            {
                ptrDSN->retransmited = true;
                retransSeg[ptrDSN->dataSeqNumber] = ptrDSN->dataLevelLength;
            }
            break;
        case F_RTO:
            sFlow->ReTxSeqNumber = std::max(sFlow->ReTxSeqNumber, ptrDSN->subflowSeqNumber + ptrDSN->dataLevelLength);
            break;
        default:
            break;
    }

    plen = (4 - (olen % 4)) % 4;
    olen = (olen + plen) / 4;
    hlen += olen;
    mptcpHeader.SetLength(hlen);
    mptcpHeader.SetOptionsLength(olen);
    mptcpHeader.SetPaddingLength(plen);

  m_mptcp->SendPacket (pkt, mptcpHeader, sFlow->sAddr, sFlow->dAddr);
  //delete ptrDSN; // if you want let it you've to uncomment 'mapDSN.erase (it)' in method GetunAckPkt
}

void
MpTcpSocketImpl::SetReTxTimeout (uint8_t sFlowIdx)
{
    MpTcpSubFlow *sFlow = subflows[sFlowIdx];
    if ( sFlow->retxEvent.IsExpired () )
    {
        Time rto = sFlow->rtt->RetransmitTimeout ();
        NS_LOG_INFO ("Schedule ReTxTimeout subflow ("<<(int)sFlowIdx<<") at time " << Simulator::Now ().GetSeconds () << " after rto ("<<rto.GetSeconds ()<<") at " << (Simulator::Now () + rto).GetSeconds ());
        sFlow->retxEvent = Simulator::Schedule (rto,&MpTcpSocketImpl::ReTxTimeout,this, sFlowIdx);
    }
}

bool
MpTcpSocketImpl::ProcessAction (uint8_t sFlowIdx, Actions_t a)
{
    //NS_LOG_FUNCTION (this << m_node->GetId()<< sFlowIdx <<m_stateMachine->printAction(a) << a );
    MpTcpSubFlow * sFlow = subflows[sFlowIdx];
    bool result = true;
    switch (a)
    {
        case SYN_TX:
            NS_LOG_LOGIC ("MpTcpSocketImpl"<<m_node->GetId()<<" " << this <<" Action: SYN_TX, Subflow: "<<sFlowIdx);
            SendEmptyPacket (sFlowIdx, TcpHeader::SYN);
            break;

        case ACK_TX:
            // this acknowledgement is not part of the handshake process
            NS_LOG_LOGIC ("MpTcpSocketImpl " << this <<" Action ACK_TX");
            SendEmptyPacket (sFlowIdx, TcpHeader::ACK);
            break;

        case FIN_TX:
            NS_LOG_LOGIC ("MpTcpSocketImpl "<<m_node->GetId()<<" "  << this <<" Action FIN_TX");
            NS_LOG_INFO  ("Number of rejected packet ("<<nbRejected<< ") total received packet (" << nbReceived <<")");
            SendEmptyPacket (sFlowIdx, TcpHeader::FIN);
            break;

        case FIN_ACK_TX:
            NS_LOG_LOGIC ("MpTcpSocketImpl "<<m_node->GetId()<<" "  << this <<" Action FIN_ACK_TX");
            NS_LOG_INFO  ("Number of rejected packet ("<<nbRejected<< ") total received packet (" << nbReceived <<")");
            SendEmptyPacket (sFlowIdx, TcpHeader::FIN | TcpHeader::ACK);
            CloseMultipathConnection();
            sFlow->state = CLOSED;
            break;

        case TX_DATA:
            NS_LOG_LOGIC ("MpTcpSocketImpl "<<m_node->GetId()<<" "  << this <<" Action TX_DATA");
            result = SendPendingData ();
            break;

        default:
            NS_LOG_LOGIC ("MpTcpSocketImpl "<<m_node->GetId()<<": " << this <<" Action: " << m_stateMachine->printAction(a) << " ( " << a << " )" << " not handled");
            break;
    }
    return result;
}

bool
MpTcpSocketImpl::ProcessAction (uint8_t sFlowIdx, MpTcpHeader mptcpHeader, Ptr<Packet> pkt, uint32_t dataLen, Actions_t a)
{
    //NS_LOG_FUNCTION (this << m_node->GetId()<< sFlowIdx <<m_stateMachine->printAction(a) << a );
    MpTcpSubFlow * sFlow = subflows[sFlowIdx];
    bool result = true;
    uint32_t seq = 0;

    switch (a)
    {
        case ACK_TX_1:
            NS_LOG_LOGIC ("MpTcpSocketImpl"<<m_node->GetId()<<" " << this <<" Action: ACK_TX_1");
            // TCP SYN consumes one byte
            if( sFlowIdx == 0)
                seq = 2;
            else
                //seq = 2;
                seq = 1; // because we send only ACK (1 packet)

            NS_LOG_INFO ("initiating RTO for subflow ("<< (int) sFlowIdx <<") with seq "<<sFlow->TxSeqNumber);
            sFlow->rtt->Init( mptcpHeader.GetAckNumber () + SequenceNumber(seq) ); // initialize next with the next seq number to be sent
            sFlow->rtt->est = TimeUnit<1>("1.5s");

            sFlow->RxSeqNumber = (uint32_t) mptcpHeader.GetSequenceNumber () + 1;
            sFlow->highestAck  = std::max ( sFlow->highestAck, (uint32_t) mptcpHeader.GetAckNumber () - 1 );

            SendEmptyPacket (sFlowIdx, TcpHeader::ACK);
            if(addrAdvertised == false)
            {
                AdvertiseAvailableAddresses();
                addrAdvertised = true;
            }
            aggregatedBandwidth += sFlow->bandwidth;
            // when a single path is established between endpoints then we can say the connection is established
            if(m_state != ESTABLISHED)
                NotifyConnectionSucceeded ();

            m_state = ESTABLISHED;
            sFlow->StartTracing ("CongestionWindow");
            break;

        case SYN_ACK_TX:
            NS_LOG_INFO ("MpTcpSocketImpl("<<m_node->GetId()<<") sFlowIdx("<< (int) sFlowIdx <<") Action SYN_ACK_TX");
            // TCP SYN consumes one byte
            sFlow->RxSeqNumber = (uint32_t) mptcpHeader.GetSequenceNumber() + 1 ;
            sFlow->highestAck  = std::max ( sFlow->highestAck, (uint32_t) mptcpHeader.GetAckNumber () - 1 );
            SendEmptyPacket (sFlowIdx, TcpHeader::SYN | TcpHeader::ACK);
            break;

        case NEW_SEQ_RX:
            NS_LOG_LOGIC ("MpTcpSocketImpl::ProcessAction -> " << this <<" Action NEW_SEQ_RX already processed in ProcessHeaderOptions");
            // Process new data received
            break;

        case NEW_ACK:
            // action performed by receiver
            NS_LOG_LOGIC ("MpTcpSocketImpl::ProcessAction -> " << this <<" Action NEW_ACK");
            NewACK (sFlowIdx, mptcpHeader, 0);
            break;

        case SERV_NOTIFY:
            // the receiver had received the ACK confirming the establishment of the connection
            NS_LOG_LOGIC ("MpTcpSocketImpl  Action SERV_NOTIFY -->  Connected!");
            sFlow->RxSeqNumber = (uint32_t) mptcpHeader.GetSequenceNumber() + 1; // next sequence to receive
            NS_LOG_LOGIC ("MpTcpSocketImpl:Serv_Notify next ACK will be = " << sFlow->RxSeqNumber);
            sFlow->highestAck  = std::max ( sFlow->highestAck, (uint32_t) mptcpHeader.GetAckNumber () - 1 );
            sFlow->connected   = true;
            if(m_connected != true)
                NotifyNewConnectionCreated (this, m_remoteAddress);
            m_connected        = true;
            break;

        default:
            result = ProcessAction ( sFlowIdx, a);
            break;
    }
    return result;
}

DSNMapping*
MpTcpSocketImpl::getAckedSegment(uint8_t sFlowIdx, uint32_t ack)
{
    MpTcpSubFlow* sFlow = subflows[sFlowIdx];
    DSNMapping* ptrDSN = 0;
    for (list<DSNMapping *>::iterator it = sFlow->mapDSN.begin(); it != sFlow->mapDSN.end(); ++it)
    {
        DSNMapping* dsn = *it;
        if(dsn->subflowSeqNumber + dsn->dataLevelLength == ack)
        {
            ptrDSN = dsn;
            break;
        }
    }
    return ptrDSN;
}

void
MpTcpSocketImpl::NewACK (uint8_t sFlowIdx, MpTcpHeader mptcpHeader, TcpOptions *opt)
{
    MpTcpSubFlow * sFlow = subflows[sFlowIdx];
    uint32_t ack = (uint32_t) mptcpHeader.GetAckNumber ();
    uint32_t ackedBytes = ack - sFlow->highestAck - 1;
    DSNMapping* ptrDSN = getAckedSegment(sFlowIdx, ack);


    if (AlgoPR == F_RTO)
    {
        uint16_t nbP[] = {389, 211, 457, 277, 367, 479, 233}; // some prime numbers
        double threshold = 0.061 * (((double) (((int) time (NULL)) % nbP[sFlowIdx])) / (double) nbP[sFlowIdx]);
        if(sFlow->nbRecvAck == -1)
            sFlow->nbRecvAck = (rejectPacket(threshold)==true ? 0:-1);
        else
        {
            sFlow->nbRecvAck++;
            if(sFlow->nbRecvAck < sFlow->cwnd)
            {
                return;
            }else
            {
                sFlow->nbRecvAck = -1;
            }
        }
    }
    if( (opt != 0) && (opt->optName == OPT_DSACK) )
    {
        OptDSACK* dsack = (OptDSACK*) opt;
        NS_LOG_WARN (Simulator::Now().GetSeconds() <<" DSACK_option -> Subflow "<<(int)sFlowIdx<<" 1stBlock lowerEdge "<<dsack->blocks[0]<<" upperEdge "<<dsack->blocks[1]<<" / 2ndBlock lowerEdge " << dsack->blocks[3] <<" upperEdge " << dsack->blocks[4] );
        DSNMapping* dsn = getAckedSegment(dsack->blocks[0], dsack->blocks[1]);
        if(ptrDSN != 0)
            NS_LOG_WARN (Simulator::Now().GetSeconds() <<" Cumulative_ACK -> "<< " localToken "<< localToken<<" Subflow "<<(int)sFlowIdx<<" Data_ACK "<<ptrDSN->dataSeqNumber + ptrDSN->dataLevelLength <<" Subflow_ACK "<< ack <<" Data_DSACK "<<dsack->blocks[0]<<" "<<dsack->blocks[1]<<" Subflow_DSACK "<<dsn->subflowSeqNumber<<" "<<dsn->subflowSeqNumber + dsn->dataLevelLength<<" highestAckedData " << sFlow->highestAck<<" maxSequenNumber " << sFlow->maxSeqNb <<" AckedData " << ackedBytes << " unAckedData " << ( sFlow->maxSeqNb - sFlow->highestAck ));
        else
            NS_LOG_WARN (Simulator::Now().GetSeconds() <<" Cumulative_ACK -> "<< " localToken "<< localToken<<" Subflow "<<(int)sFlowIdx<<" Data_ACK ? Subflow_ACK "<< ack <<" Data_DSACK "<<dsack->blocks[0]<<" "<<dsack->blocks[1]<<" Subflow_DSACK "<<dsn->subflowSeqNumber<<" "<<dsn->subflowSeqNumber + dsn->dataLevelLength<<" highestAckedData " << sFlow->highestAck<<" maxSequenNumber " << sFlow->maxSeqNb <<" AckedData " << ackedBytes << " unAckedData " << ( sFlow->maxSeqNb - sFlow->highestAck ));
    }else
    {
        if(ptrDSN != 0)
            NS_LOG_WARN (Simulator::Now().GetSeconds() <<" Cumulative_ACK -> "<< " localToken "<< localToken<<" Subflow "<<(int)sFlowIdx<<" Data_ACK "<<ptrDSN->dataSeqNumber + ptrDSN->dataLevelLength <<" Subflow_ACK "<< ack <<" highestAckedData " << sFlow->highestAck<<" maxSequenNumber " << sFlow->maxSeqNb <<" AckedData " << ackedBytes << " unAckedData " << ( sFlow->maxSeqNb - sFlow->highestAck ));
        else
            NS_LOG_WARN (Simulator::Now().GetSeconds() <<" Cumulative_ACK -> "<< " localToken "<< localToken<<" Subflow "<<(int)sFlowIdx<<" Data_ACK ? Subflow_ACK "<< ack <<" highestAckedData " << sFlow->highestAck<<" maxSequenNumber " << sFlow->maxSeqNb <<" AckedData " << ackedBytes << " unAckedData " << ( sFlow->maxSeqNb - sFlow->highestAck ));
    }

    if( IsDuplicatedAck(sFlowIdx, mptcpHeader, opt) == false )
    {
        sFlow->retxEvent.Cancel (); //On recieving a "New" ack we restart retransmission timer .. RFC 2988

        sFlow->updateRTT      (mptcpHeader.GetAckNumber (), Simulator::Now ());
        sFlow->RxSeqNumber  = (uint32_t) mptcpHeader.GetSequenceNumber() + 1;
        sFlow->highestAck   = std::max ( sFlow->highestAck, ack - 1 );
        unAckedDataCount    = ( sFlow->maxSeqNb - sFlow->highestAck ) ;

        if ( unAckedDataCount > 0 )
        {
            Time rto = sFlow->rtt->RetransmitTimeout ();
            NS_LOG_LOGIC ("Schedule ReTxTimeout at " << Simulator::Now ().GetSeconds () << " to expire at " << (Simulator::Now () + rto).GetSeconds () <<" unAcked data "<<unAckedDataCount);
            sFlow->retxEvent = Simulator::Schedule (rto, &MpTcpSocketImpl::ReTxTimeout, this, sFlowIdx);
        }
        // you have to move the idxBegin of the sendingBuffer by the amount of newly acked data
        OpenCWND (sFlowIdx, ackedBytes);
        NotifyDataSent ( GetTxAvailable() );
        SendPendingData ();
    }else if( (useFastRecovery == true) || (AlgoPR == F_RTO && frtoStep == Step_4) )
    {
        // remove sequence gap from DNSMap list
        NS_LOG_WARN (Simulator::Now ().GetSeconds () << " Fast Recovery -> duplicated ACK ("<< (uint32_t) mptcpHeader.GetAckNumber () <<")");
        OpenCWND (sFlowIdx, 0);
        SendPendingData ();
    }
}

Actions_t
MpTcpSocketImpl::ProcessEvent (uint8_t sFlowId, Events_t e)
{
    //NS_LOG_FUNCTION(this << (int) sFlowId << m_stateMachine->printEvent(e));
    MpTcpSubFlow * sFlow = subflows[sFlowId];
    if( sFlow == 0 )
        return NO_ACT;
    States_t previous = sFlow->state;
    SA sAct = m_stateMachine->Lookup(sFlow->state, e);
    if( previous == LISTEN && sAct.state == SYN_RCVD && sFlow->connected == true )
        return NO_ACT;

    sFlow->state = sAct.state;
    NS_LOG_LOGIC ("MpTcpSocketImpl"<<m_node->GetId()<<":ProcessEvent Moved from subflow "<<(int)sFlowId <<" state " << m_stateMachine->printState(previous) << " to " << m_stateMachine->printState(sFlow->state));

    if (!m_connected && previous == SYN_SENT && sFlow->state == ESTABLISHED)
    {
        // this means the application side has completed its portion of the handshaking
        //Simulator::ScheduleNow(&NotifyConnectionSucceeded(), this);
        m_connected = true;
        m_endPoint->SetPeer (m_remoteAddress, m_remotePort);
    }
    return sAct.action;
}

void
MpTcpSocketImpl::SendEmptyPacket (uint8_t sFlowId, uint8_t flags)
{
  //NS_LOG_FUNCTION (this << (int) sFlowId << (uint32_t)flags);
  MpTcpSubFlow *sFlow = subflows[sFlowId];
  Ptr<Packet> p = new Packet(0); //Create<Packet> ();
  MpTcpHeader header;
  uint8_t hlen = 0;
  uint8_t olen = 0;

  header.SetSourcePort      (sFlow->sPort);
  header.SetDestinationPort (sFlow->dPort);
  header.SetFlags           (flags);
  header.SetSequenceNumber  (sFlow->TxSeqNumber);
  header.SetAckNumber       (sFlow->RxSeqNumber);
  header.SetWindowSize      (AdvertisedWindowSize());

  if(((sFlow->state == SYN_SENT) || (sFlow->state==SYN_RCVD && mpEnabled==true)) && mpSendState==MP_NONE)
  {
      mpSendState = MP_MPC;
      localToken  = rand() % 1000;
      header.AddOptMPC(OPT_MPC, localToken);
      olen += 5;
  }

  uint8_t plen = (4 - (olen % 4)) % 4;
  // urgent pointer
  // check sum filed
  olen = (olen + plen) / 4;
  hlen = 5 + olen;
  header.SetLength(hlen);
  header.SetOptionsLength(olen);
  header.SetPaddingLength(plen);

  //SetReTxTimeout (sFlowId);

  m_mptcp->SendPacket (p, header, sFlow->sAddr, sFlow->dAddr);
  //sFlow->rtt->SentSeq (sFlow->TxSeqNumber, 1);           // notify the RTT
  sFlow->TxSeqNumber++;
  sFlow->maxSeqNb++;
  //unAckedDataCount++;
}

void
MpTcpSocketImpl::SendAcknowledge (uint8_t sFlowId, uint8_t flags, TcpOptions *opt)
{
  //NS_LOG_FUNCTION (this << (int) sFlowId << (uint32_t)flags);
  NS_LOG_INFO ("sending acknowledge segment with option");
  MpTcpSubFlow *sFlow = subflows[sFlowId];
  Ptr<Packet> p = new Packet(0); //Create<Packet> ();
  MpTcpHeader header;
  uint8_t hlen = 0;
  uint8_t olen = 0;

  header.SetSourcePort      (sFlow->sPort);
  header.SetDestinationPort (sFlow->dPort);
  header.SetFlags           (flags);
  header.SetSequenceNumber  (sFlow->TxSeqNumber);
  header.SetAckNumber       (sFlow->RxSeqNumber);
  header.SetWindowSize      (AdvertisedWindowSize());

    switch ( AlgoPR )
    {
        case Eifel:
            header.AddOptTT (OPT_TT, ((OptTimesTamp *) opt)->TSval, ((OptTimesTamp *) opt)->TSecr);
            olen += 17;
            // I've to think about if I can increment or not the sequence control parameters
            sFlow->TxSeqNumber++;
            sFlow->maxSeqNb++;
            break;
        case D_SACK:
            header.AddOptDSACK (OPT_DSACK, (OptDSACK *) opt);
            olen += 33;
            break;
        default:
            break;
    }

  uint8_t plen = (4 - (olen % 4)) % 4;
  // urgent pointer
  // check sum filed
  olen = (olen + plen) / 4;
  hlen = 5 + olen;
  header.SetLength(hlen);
  header.SetOptionsLength(olen);
  header.SetPaddingLength(plen);
  m_mptcp->SendPacket (p, header, sFlow->sAddr, sFlow->dAddr);

}

void
MpTcpSocketImpl::allocateSendingBuffer (uint32_t size)
{
    //NS_LOG_FUNCTION(this << size);
    sendingBuffer = new DataBuffer(size);
}

void
MpTcpSocketImpl::allocateRecvingBuffer (uint32_t size)
{
    //NS_LOG_FUNCTION(this << size);
    recvingBuffer = new DataBuffer(size);
}

void
MpTcpSocketImpl::SetunOrdBufMaxSize (uint32_t size)
{
    unOrdMaxSize = size;
}

uint32_t
MpTcpSocketImpl::Recv (uint8_t* buf, uint32_t size)
{
  //NS_LOG_FUNCTION (this << size);
  //Null packet means no data to read, and an empty packet indicates EOF
  uint32_t toRead = std::min( recvingBuffer->PendingData() , size);
  return recvingBuffer->Retrieve(buf, toRead);
}

void
MpTcpSocketImpl::ForwardUp (Ptr<Packet> p, Ipv4Address ipv4Remote, uint16_t port)
{

  m_remoteAddress = ipv4Remote; //m_endPoint->GetPeerAddress();

  m_remotePort    = m_endPoint->GetPeerPort();
  m_localAddress  = m_endPoint->GetLocalAddress();

  uint8_t sFlowIdx = LookupByAddrs(m_localAddress, ipv4Remote); //m_endPoint->GetPeerAddress());

  if(! (sFlowIdx < MaxSubFlowNumber) )
      return;

  MpTcpSubFlow *sFlow = subflows[sFlowIdx];

  MpTcpHeader mptcpHeader;
  p->RemoveHeader (mptcpHeader);

  uint32_t dataLen;   // packet's payload length
  remoteRecvWnd = (uint32_t) mptcpHeader.GetWindowSize (); //update the flow control window

  //Events_t event = SimulationSingleton<TcpStateMachine>::Get ()->FlagsEvent (tcpHeader.GetFlags () );
  sFlow->dAddr = ipv4Remote;//m_endPoint->GetPeerAddress();
  sFlow->dPort = m_endPoint->GetPeerPort();
  // process the options in the header
  Actions_t action = ProcessHeaderOptions(sFlowIdx, p, &dataLen, mptcpHeader);

  //NS_LOG_INFO("MpTcpSocketImpl"<<m_node->GetId()<<":ForwardUp Socket " << this << " ( current state " << m_stateMachine->printState( sFlow->state ) << " ) -> processing packet action is " << m_stateMachine->printAction( action ) );

  ProcessAction (sFlowIdx, mptcpHeader, p, dataLen, action);
}

void
MpTcpSocketImpl::ProcessMultipathState ()
{
    NS_LOG_FUNCTION_NOARGS();
    switch(mpState)
    {
        case MP_ADDR:
            mpState = MP_JOIN;
            InitiateSubflows();
            break;
        default:
            break;
    }
}

bool
MpTcpSocketImpl::InitiateSubflows ()
{
    //NS_LOG_FUNCTION_NOARGS();
    bool result = true;
    for(uint32_t i =0; i < localAddrs.size(); i++)
    for(uint32_t j =i; j < remoteAddrs.size(); j++)
    {
        uint8_t addrID     = localAddrs[i]->addrID;
        Ipv4Address local  = localAddrs[i]->ipv4Addr;
        Ipv4Address remote = remoteAddrs[j]->ipv4Addr;

        // skip already established flows
        if( ((local == m_localAddress) && (remote == m_remoteAddress)) || (!IsThereRoute(local, remote)))
            continue;
NS_LOG_LOGIC ("IsThereRoute -> Route from srcAddr "<< local << " to dstAddr " << remote <<", exist !");
        uint32_t initSeqNb = rand() % 1000 + (sfInitSeqNb.size() +1 ) * 10000;
        sfInitSeqNb[local] = initSeqNb;
        Ptr<Packet> pkt = Create<Packet> ();
        MpTcpHeader header;
        header.SetFlags           (TcpHeader::SYN);//flags);
        header.SetSequenceNumber  (initSeqNb);
        header.SetAckNumber       (subflows[0]->RxSeqNumber);
        // endpoints port number remain unchangeable
        header.SetSourcePort      (m_endPoint->GetLocalPort ());
        header.SetDestinationPort (m_remotePort);

        header.AddOptJOIN         (OPT_JOIN, remoteToken, addrID);

        uint8_t olen = 6;
        uint8_t plen = (4 - (olen % 4)) % 4;

        header.SetWindowSize ( AdvertisedWindowSize() );
        // urgent pointer
        // check sum filed
        olen = (olen + plen) / 4;
        uint8_t hlen = 5 + olen;
        header.SetLength(hlen);
        header.SetOptionsLength(olen);
        header.SetPaddingLength(plen);

        //SetReTxTimeout (sFlowIdx);
        m_mptcp->SendPacket (pkt, header, local, remote);
NS_LOG_INFO("MpTcpSocketImpl::InitiateSubflows -> (src, dst) = (" << local << ", " << remote << ") JOIN segment successfully sent !");

    }
    return result;
}

void
MpTcpSocketImpl::calculateTotalCWND ()
{
    totalCwnd = 0;
    for (uint32_t i = 0; i < subflows.size() ; i++)
    {
        totalCwnd += subflows[i]->cwnd;
    }
}

Actions_t
MpTcpSocketImpl::ProcessHeaderOptions (uint8_t sFlowIdx, Ptr<Packet> pkt, uint32_t *dataLen, MpTcpHeader mptcpHeader)
{
    // packet is without header, see ForwardUp method
    //NS_LOG_FUNCTION(this);
    MpTcpSubFlow * sFlow = subflows[sFlowIdx];
    vector< TcpOptions* > options = mptcpHeader.GetOptions();
    uint8_t flags = mptcpHeader.GetFlags ();

  Actions_t action = ProcessEvent (sFlowIdx, m_stateMachine->FlagsEvent (flags)); //updates the state
  //NS_LOG_INFO("MpTcpSocketImpl::ProcessHeaderOptions-> event  ("<< m_stateMachine->printEvent(m_stateMachine->FlagsEvent (flags))<<") => action = "<< m_stateMachine->printAction( action ));
  int length = 0;
  TcpOptions *opt, *opt1;

  bool hasSyn = flags &  TcpHeader::SYN;
  //bool hasFin = flags &  TcpHeader::FIN;
  //bool isAck  = flags == TcpHeader::ACK;
  bool TxAddr = false, TxACK = false, TxAckOpt = false;
  bool initSubFlow = false;

  for(uint32_t j = 0; j < options.size(); j++)
  {
      opt = options[j];

      if ( (opt->optName == OPT_MPC) && hasSyn && (mpRecvState == MP_NONE) )
      {
          //NS_LOG_INFO("MpTcpSocketImpl:ProcessHeaderOptions -> OPT_MPC received");
          mpRecvState = MP_MPC;
          mpEnabled   = true;
          remoteToken = ((OptMultipathCapable *)opt)->senderToken;

      }else if((opt->optName == OPT_JOIN) && hasSyn)
      {
          OptJoinConnection * optJoin = (OptJoinConnection *) opt;
          if( (mpSendState == MP_ADDR) && (localToken == optJoin->receiverToken) )
          {
              // Join option is sent over the path (couple of addresses) not already in use
              //NS_LOG_INFO ( "MpTcpSocketImpl::ProcessHeaderOptions -> OPT_JOIN received");
              initSubFlow = true;
          }

      }else if((opt->optName == OPT_ADDR) && (mpRecvState==MP_MPC))
      {
          // necessary action must be done here
          TxAddr = true;
          MpTcpAddressInfo * addrInfo = new MpTcpAddressInfo();
          addrInfo->addrID   = ((OptAddAddress *)opt)->addrID;
          addrInfo->ipv4Addr = ((OptAddAddress *)opt)->addr;
          remoteAddrs.insert  (remoteAddrs.end(), addrInfo);
          //NS_LOG_INFO ( "MpTcpSocketImpl::ProcessHeaderOptions -> remote address " << addrInfo->ipv4Addr );
      }else if(opt->optName == OPT_REMADR)
      {
          length += 2;
      }else if(opt->optName == OPT_TT)
      {
          NS_LOG_INFO ("TCP TimesTamp option");
          if(server == true)
          {
              opt1 = new OptTimesTamp (OPT_TT, Simulator::Now ().GetMilliSeconds (), ((OptTimesTamp *) opt)->TSval);
          }else if(client == true)
          {
              NS_LOG_INFO ("This is a client");
              opt1 = (OptTimesTamp *) opt;
          }
          TxAckOpt = true;
      }else if (opt->optName == OPT_DSACK && client == true)
      {
          NS_LOG_INFO ("Client received DSACK option");
          opt1 = (OptDSACK *) opt;
          TxAckOpt = true;
      }else if(opt->optName == OPT_DSN)
      {
          // data reception so threat it
          OptDataSeqMapping * optDSN = (OptDataSeqMapping *) opt;
          TxACK = true;
          *dataLen = optDSN->dataLevelLength;
          Ptr<Packet> packet = pkt;
          uint32_t pktLen    = *dataLen;

NS_LOG_LOGIC("Multipath Seq N° dataSeqNumber (" << optDSN->dataSeqNumber <<") Seq N° nextRxSequence (" << nextRxSequence<<")   /   Subflow Seq N° RxSeqNumber (" << sFlow->RxSeqNumber << ") Seq N° subflowSeqNumber (" << optDSN->subflowSeqNumber<< ")");

          if ( (AlgoPR == D_SACK) && (optDSN->dataSeqNumber > nextRxSequence) )
          {
              NS_LOG_DEBUG ("Subflow ("<<(int)sFlowIdx<<"): data arrived ("<< optDSN->dataSeqNumber <<") of length ("<< optDSN->dataLevelLength <<") buffered for subsequent reordering !");
              StoreUnOrderedData (new DSNMapping(sFlowIdx, optDSN->dataSeqNumber, optDSN->dataLevelLength, optDSN->subflowSeqNumber, mptcpHeader.GetAckNumber (), pkt));
              // send ACK with DSACK only for segment that also create a hole in the subflow level
              //if(optDSN->subflowSeqNumber > sFlow->RxSeqNumber)
              //{
                  NS_LOG_DEBUG ("DSACK option to be created !");
                  opt1 = createOptDSACK (optDSN);
                  TxAckOpt = true;
              //}
              TxACK = false;
          }else if( optDSN->subflowSeqNumber == sFlow->RxSeqNumber )
          {
              if( optDSN->dataSeqNumber == nextRxSequence )
              {
                  // it's ok for this data, send ACK( sFlowSeq + dataLevel) and save data to reception buffer
                  NS_LOG_LOGIC("MpTcpSocketImpl::ProcessHeaderOptions -> acknowledgment for data length ("<< optDSN->dataLevelLength <<") to be sent on subflow (" << (int) sFlowIdx << ") remote address (" << sFlow->dAddr<<")");

                  uint32_t amountRead = recvingBuffer->ReadPacket (pkt, pktLen);
                  sFlow->RxSeqNumber += amountRead;
                  nextRxSequence     += amountRead;
                  sFlow->highestAck   = std::max ( sFlow->highestAck, (uint32_t) mptcpHeader.GetAckNumber () - 1 );
                  // acknowledgement for this segment will be sent because we've already set TxACK
                  ReadUnOrderedData ();
                  NotifyDataRecv    ();

              }else if( optDSN->dataSeqNumber > nextRxSequence )
              {
                  // it's ok for the subflow but not for the connection -> put the data on resequency buffer
                  NS_LOG_DEBUG("Subflow ("<<(int)sFlowIdx<<"): data arrived ("<< optDSN->dataSeqNumber <<") of length ("<< optDSN->dataLevelLength <<") buffered for subsequent reordering !");
                  TxACK = StoreUnOrderedData(new DSNMapping(sFlowIdx, optDSN->dataSeqNumber, optDSN->dataLevelLength, optDSN->subflowSeqNumber, mptcpHeader.GetAckNumber (), pkt));

                  // we send an ACK back for the received segment not in sequence
                  TxACK = false;
                  //sFlow->RxSeqNumber += pktLen; // the received data is in sequence of the subflow => ack it's reception
              }else
              {
                  NS_LOG_LOGIC("MpTcpSocketImpl::ProcessHeaderOptions -> Subflow ("<<(int)sFlowIdx<<"): data received but duplicated, reject ("<<optDSN->subflowSeqNumber<<")");
              }
          }else if( optDSN->subflowSeqNumber > sFlow->RxSeqNumber )
          {
              NS_LOG_LOGIC("MpTcpSocketImpl::ProcessHeaderOptions -> Subflow ("<<(int)sFlowIdx<<"): data arrived ("<<optDSN->subflowSeqNumber<<") causing potontial data lost !");
              TxACK = StoreUnOrderedData(new DSNMapping(sFlowIdx, optDSN->dataSeqNumber, optDSN->dataLevelLength, optDSN->subflowSeqNumber, mptcpHeader.GetAckNumber (), pkt));
              //TxACK = false;
          }else
          {
              NS_LOG_LOGIC("MpTcpSocketImpl::ProcessHeaderOptions -> Subflow ("<<(int)sFlowIdx<<"): data already received, reject !");
              //action = NO_ACT;
          }
      }
  }

  if(TxAddr==true)
  {
    mpRecvState = MP_ADDR;
    sFlow->RxSeqNumber ++;
    sFlow->highestAck++;

    action = NO_ACT;
    if (mpSendState!=MP_ADDR)
    {
        AdvertiseAvailableAddresses(); // this is what the receiver has to do
    }
    else if (mpSendState==MP_ADDR)
    {
        sFlow->highestAck++; // we add 2 to highestAck because ACK, ADDR segments send it before
        InitiateSubflows();  // this is what the initiator has to do
    }
  }

  if(TxAckOpt == true)
  {
      if(server == true)
      {
          SendAcknowledge (sFlowIdx, TcpHeader::ACK, opt1);
      }
      else if(client == true)
      {
          NewACK (sFlowIdx, mptcpHeader, opt1);
      }
      action = NO_ACT;
  }else if (TxACK == true)
  {

NS_LOG_INFO ( "Recv: Subflow ("<<(int) sFlowIdx <<") TxSeq (" << sFlow->TxSeqNumber <<") RxSeq (" << sFlow->RxSeqNumber <<")\n" );

      SendEmptyPacket (sFlowIdx, TcpHeader::ACK);
      action = NO_ACT;
  }
  return action;
}

OptDSACK*
MpTcpSocketImpl::createOptDSACK (OptDataSeqMapping * optDSN)
{
    OptDSACK* ptrDSAK = new OptDSACK (OPT_DSACK);
    // Add the received segment which generated the currently prepared acknowledgement
    ptrDSAK->AddfstBlock(optDSN->dataSeqNumber, optDSN->dataSeqNumber + optDSN->dataLevelLength);

    uint64_t upperBound = 0;
    uint64_t lowerBound = 0;
    if ( unOrdered.size() == 1 )
    {
        DSNMapping *ptr1 = (* (unOrdered.begin()));
        lowerBound = ptr1->dataSeqNumber;
        upperBound = ptr1->dataSeqNumber + ptr1->dataLevelLength;
    }
    else
    {
        map<uint64_t, uint32_t> blocs;
        list<DSNMapping *>::iterator current = unOrdered.begin();
        list<DSNMapping *>::iterator next = unOrdered.begin();
        ++next;

        while( next != unOrdered.end() )
        {
            DSNMapping *ptr1 = *current;
            DSNMapping *ptr2 = *next;
            uint32_t  length = 0;
            uint64_t  dsn1   = ptr1->dataSeqNumber;
            uint64_t  dsn2   = ptr2->dataSeqNumber;

            if ( blocs.find ( dsn1 ) != blocs.end() )
            {
                length = blocs [dsn1];
            }
            else
            {
                length       = ptr1->dataLevelLength;
                blocs[dsn1]  = ptr1->dataLevelLength;
            }

            if ( dsn1 + length == dsn2 )
            {
                blocs[dsn1] = blocs[dsn1] + ptr2->dataLevelLength;
            }
            else
            {
                current = next;
            }
            ++next;
        }
        DSNMapping *ptr1 = (* (unOrdered.begin()));
        lowerBound = ptr1->dataSeqNumber;
        upperBound = ptr1->dataSeqNumber + blocs[ptr1->dataSeqNumber];

        NS_LOG_INFO ("createOptDSACK -> blocs size = " << blocs.size() );
    }
    ptrDSAK->AddBlock( lowerBound, upperBound);
    return ptrDSAK;
}

void
MpTcpSocketImpl::ReadUnOrderedData ()
{
    //NS_LOG_FUNCTION (this);
    list<DSNMapping *>::iterator current = unOrdered.begin();
    list<DSNMapping *>::iterator next = unOrdered.begin();

    // I changed this method, now whenever a segment is readed it get dropped from that list
    while(next != unOrdered.end())
    {
        ++next;
        DSNMapping *ptr   = *current;
        uint32_t sFlowIdx = ptr->subflowIndex;
        MpTcpSubFlow *sFlow = subflows[ sFlowIdx ];
        if ( (ptr->dataSeqNumber <= nextRxSequence) && (ptr->subflowSeqNumber == sFlow->RxSeqNumber) )
        {
            uint32_t amount = recvingBuffer->Add (ptr->packet, ptr->dataLevelLength);

            if(amount == 0)
                break; // reception buffer is full

            sFlow->RxSeqNumber += amount;
            sFlow->highestAck   = std::max ( sFlow->highestAck, ptr->acknowledgement - 1 );
            nextRxSequence     += amount;
            NS_LOG_INFO ("ReadUnOrderedData("<<unOrdered.size()<<") -> in sequence data (" << amount<<") found saved => Acknowledgement ("<<sFlow->RxSeqNumber<<") data seq ("<<ptr->dataSeqNumber<<") sent on subflow ("<< sFlowIdx<<")." );
            /**
             * Send an acumulative acknowledge
             */
            switch( AlgoPR )
            {
                case Eifel:
                    //SendAcknowledge (sFlowIdx, TcpHeader::ACK, new OptTimesTamp (OPT_TT, Simulator::Now ().GetMilliSeconds (), ((OptTimesTamp *) opt)->TSval));
                    break;
                case D_SACK:
                    // don't send an ACK for already acked segment
                    break;
                default:
                    //SendEmptyPacket (sFlowIdx, TcpHeader::ACK);
                    break;
            }
            SendEmptyPacket (sFlowIdx, TcpHeader::ACK);
            NotifyDataRecv ();
            unOrdered.erase( current );
        }
        current = next;
    }
}

uint8_t
MpTcpSocketImpl::ProcessOption (TcpOptions *opt)
{
    uint8_t originalSFlow = 255;
    if( opt != 0 ) {
    if( opt->optName == OPT_DSACK )
    {
        OptDSACK * dsack = (OptDSACK *) opt;
        // fstLeft = dsack->blocks[0];         fstRight = dsack->blocks[1];
        uint64_t fstLeft = dsack->blocks[0], fstRight = dsack->blocks[1];
        uint64_t sndLeft = dsack->blocks[2], sndRight = dsack->blocks[3];
        NS_LOG_DEBUG("ProcessOption -> sndLeft ("<<sndLeft<<") sndRight ("<<sndRight<<")");
        /**
         * go throw each sent packet which is pending for un ACK, and check if in the received option there is a ACK at the data level
         * we prefer to drop the packet because we will not receive ACK for it
         */

        for (uint8_t i = 0; i < subflows.size(); i++)
        {
            MpTcpSubFlow *sFlow = subflows[i];
            list<DSNMapping *>::iterator current = sFlow->mapDSN.begin();
            list<DSNMapping *>::iterator next    = sFlow->mapDSN.begin();
            while( current != sFlow->mapDSN.end() )
            {
                ++next;
                DSNMapping *ptrDSN = *current;
                if ( ((ptrDSN->dataSeqNumber == fstLeft) && (ptrDSN->dataSeqNumber + ptrDSN->dataLevelLength == fstRight))
                    ||
                     ((ptrDSN->dataSeqNumber >= sndLeft) && (ptrDSN->dataSeqNumber + ptrDSN->dataLevelLength <= sndRight)) )
                {
                    NS_LOG_DEBUG("acked segment with DSACK -> subflowSeqNumber ("<<ptrDSN->subflowSeqNumber<<")");
                    /**
                     * increment the number of ack received for that data level sequence number
                     */
                    if( ackedSeg.find(ptrDSN->dataSeqNumber) != ackedSeg.end() )
                        ackedSeg[ ptrDSN->dataSeqNumber ] = ackedSeg[ ptrDSN->dataSeqNumber ] + 1;
                    else
                        ackedSeg[ ptrDSN->dataSeqNumber ] = 1;
                    /**
                     * By checking the data level sequence number in the received TCP header option
                     * we can deduce that the current segment was correctly received, so must be removed from DSNMapping list
                     */
                    /*
                    next = sFlow->mapDSN.erase( current );
                    delete ptrDSN;
                    */
                }
                current = next;
            }
        }
    }
    }
    return originalSFlow;
}

bool
MpTcpSocketImpl::IsDuplicatedAck (uint8_t sFlowIdx, MpTcpHeader l4Header, TcpOptions *opt)
{
    //NS_LOG_FUNCTION (this << (int)sFlowIdx << (uint32_t) l4Header.GetAckNumber ());
    uint32_t ack = (uint32_t) l4Header.GetAckNumber ();
    bool duplicated = false;

    //uint8_t originalSFlow =
    ProcessOption (opt);
    MpTcpSubFlow *sFlow = subflows[sFlowIdx];

    if ( ack < sFlow->TxSeqNumber )
    {
        // this acknowledgment don't ack the last sent data, so check to find the duplicated
        list<DSNMapping *>::iterator current = sFlow->mapDSN.begin();
        list<DSNMapping *>::iterator next = sFlow->mapDSN.begin();
        while( current != sFlow->mapDSN.end() )
        {
            ++next;
            DSNMapping *ptrDSN = *current;

            NS_LOG_LOGIC ("IsDupAck -> subflow seq n° ("<< ptrDSN->subflowSeqNumber <<") data length " << ptrDSN->dataLevelLength);
            if ( ptrDSN->subflowSeqNumber + ptrDSN->dataLevelLength <= ack )
            {
                /**
                 * increment the number of ack received for that data level sequence number
                 */
                if( ackedSeg.find(ptrDSN->dataSeqNumber) != ackedSeg.end() )
                    ackedSeg[ ptrDSN->dataSeqNumber ] = ackedSeg[ ptrDSN->dataSeqNumber ] + 1;
                else
                    ackedSeg[ ptrDSN->dataSeqNumber ] = 1;
                /**
                 * this segment was correctly acked, so must be removed from DSNMapping list
                 */
                /*
                next = sFlow->mapDSN.erase( current );
                delete ptrDSN;
                */
            }
            else if ( (ptrDSN->subflowSeqNumber == ack) && (ack - 1 <= sFlow->highestAck) )
            {
            // this may have to be retransmitted
                NS_LOG_INFO("IsDupAck -> duplicated ack for " << ack);
                duplicated = true;
                switch ( AlgoPR )
                {
                    case Eifel:
                        if( (ptrDSN->retransmited == true) && (ptrDSN->tsval > ((OptTimesTamp *) opt)->TSecr) )
                        {
                            // spurious Retransmission
                            NS_LOG_WARN ("A Spurious Retransmission detected ->");
                            double rtt = sFlow->rtt->est.GetSeconds();
                            NS_LOG_WARN (Simulator::Now().GetSeconds() <<" MpTcpSocketImpl -> "<< " localToken "<< localToken<<" Subflow "<< (int)sFlowIdx <<": RTT "<< sFlow->rtt->est.GetSeconds() <<" Moving cwnd from " << sFlow->cwnd << " to " << sFlow->savedCWND << " Throughput "<<(sFlow->cwnd * sFlow->MSS * 8)/rtt<< " GlobalThroughput "<<getGlobalThroughput());
                            sFlow->cwnd = sFlow->savedCWND;
                            sFlow->ssthresh = sFlow->savedSSThresh;
                        }else
                        {
                            DupAck (sFlowIdx, ptrDSN);
                        }
                        break;
                    case D_SACK:
                        //if( (opt != 0) && (IsRetransmitted (((OptDSACK *) opt)->blocks[0], ((OptDSACK *) opt)->blocks[1]) == true) )

                        if(opt != 0)
                        {
                            NS_LOG_DEBUG ("received ACK with DSACK option !");
                            DupDSACK (sFlowIdx, l4Header, (OptDSACK *) opt);
                        }else
                        {
                            NS_LOG_DEBUG ("received ACK without DSACK option !");
                            DupAck (sFlowIdx, ptrDSN);
                        }
                        break;
                    case F_RTO:
                        break;
                    case TCP_DOOR:
                        break;
                    case NoPR_Algo:
                        DupAck (sFlowIdx, ptrDSN);
                        break;
                    default:
                        break;
                }
                break;
            }
            current = next;
        }
        if( (AlgoPR == F_RTO) && (sFlow->phase == RTO_Recovery) )
        {
            sFlow->ackCount ++;
            switch ( frtoStep )
            {
                case Step_2:
                    if ( sFlow->ackCount == 1 )
                    {   // first acknowledgement after a RTO timeout
                        sFlow->recover = sFlow->TxSeqNumber;
                        if( (duplicated == true) || (ack == sFlow->recover) || (ack <= sFlow->ReTxSeqNumber) )
                        {
                    /**
                     * revert to the conventional RTO recovery and continue by
                     * retransmitting unacknowledged data in slow start.
                     * Do not enter step 3 of this algorithm.
                     * The SpuriousRecovery variable remains false
                     */
                            NS_LOG_WARN ("4th dupAck -> Fast Recovery: sending new packet in slow start");
                            frtoStep = Step_4;

                        }else if( (duplicated == false) && (ack < sFlow->recover) )
                        {
                    /**
                     * transmit up to two new (previously unsent) segments
                     * enter step 3 of this algorithm
                     */
                            frtoStep = Step_3;
                            if( (sendingBuffer->Empty() == true) || (std::min (AvailableWindow (sFlowIdx), sendingBuffer->PendingData()) == 0) )
                            {
                                frtoStep = Step_4;
                            }
                        }
                    }else
                    {
                        frtoStep = Step_4;
                    }
                    break;

                case Step_3:
                    if(sFlow->ackCount == 2)
                    {   // second acknowledgement after a RTO timeout
                        if( duplicated == true )
                        {
                            sFlow->cwnd = 3;
                            frtoStep = Step_4;
                        }else if( ack > sFlow->highestAck + 1 )
                        {
                            sFlow->SpuriousRecovery = true;
                            sFlow->recover = sFlow->TxSeqNumber;
                            NS_LOG_WARN ("F-RTO -> A spurious timeout have been detected !");
                        }
                    }else
                    {
                        frtoStep = Step_4;
                    }
                    break;

                default:
                    break;
            }

        }
    }
    return duplicated;
}

void
MpTcpSocketImpl::DupDSACK (uint8_t sFlowIdx, MpTcpHeader mptcpHeader, OptDSACK *dsack)
{
    uint64_t leftEdge  = dsack->blocks[0];
    //uint64_t rightEdge = dsack->blocks[1];
    NS_LOG_DEBUG("ackedSeg size = "<<ackedSeg.size());
    MpTcpSubFlow *originalSFlow = subflows[sFlowIdx];
    DSNMapping *notAckedPkt = 0;
    for (uint8_t i = 0; i < subflows.size(); i++)
    {
        MpTcpSubFlow *sFlow = subflows[i];
        list<DSNMapping *>::iterator current = sFlow->mapDSN.begin();
        list<DSNMapping *>::iterator next    = sFlow->mapDSN.begin();

        while( current != sFlow->mapDSN.end() )
        {
            ++next;
            DSNMapping *ptrDSN = *current;

                NS_LOG_DEBUG("ptrDSN->subflowSeqNumber ("<<ptrDSN->subflowSeqNumber<<") sFlow->highestAck ("<<sFlow->highestAck<<")");
            if ( (ackedSeg.find(ptrDSN->dataSeqNumber) == ackedSeg.end()) && (ptrDSN->subflowSeqNumber == sFlow->highestAck + 1) )
            {
                NS_LOG_DEBUG("updating notAckedPkt");
                // that's the first segment not already acked (by DSACK) in the current subflow
                if (notAckedPkt == 0)
                {
                    notAckedPkt = ptrDSN;
                    originalSFlow  = sFlow;
                }else if(notAckedPkt->dataSeqNumber > ptrDSN->dataSeqNumber)
                {
                    if(lastRetransmit == 0)
                    {
                        lastRetransmit = ptrDSN;
                        notAckedPkt    = ptrDSN;
                        originalSFlow  = sFlow;
                    }else if(lastRetransmit->dataSeqNumber < ptrDSN->dataSeqNumber)
                    {
                        lastRetransmit = ptrDSN;
                        notAckedPkt    = ptrDSN;
                        originalSFlow  = sFlow;
                    }
                }
            }
            current = next;
        }
    }

    if( (retransSeg.find(leftEdge) != retransSeg.end()) && (ackedSeg.find(leftEdge) != ackedSeg.end()) && (ackedSeg[leftEdge] > 1) )
    {
                                /**
                                 * if the segment reported in DSACK has been retransmitted and it's acked more than once (duplicated)
                                 * spurious congestion is detected, set the variables needed to a slow start
                                 */
        originalSFlow->phase = DSACK_SS;
        NS_LOG_WARN ("A Spurious Retransmission detected => trigger a slow start to the previous saved cwnd value!");
    }else
    {
        if(notAckedPkt != 0)
            DupAck (originalSFlow->routeId, notAckedPkt);
    }
}

void
MpTcpSocketImpl::DupAck (uint8_t sFlowIdx, DSNMapping * ptrDSN)
{
    MpTcpSubFlow *sFlow = subflows[ sFlowIdx ];
    ptrDSN->dupAckCount++;
    if ( ptrDSN->dupAckCount == 3 )
    {
        NS_LOG_WARN (Simulator::Now().GetSeconds() <<" DupAck -> Subflow ("<< (int)sFlowIdx <<") 3rd duplicated ACK for segment ("<<ptrDSN->subflowSeqNumber<<")");

        sFlow->rtt->pktRetransmit (ptrDSN->subflowSeqNumber); // notify the RTT
        //sFlow->rtt->SentSeq (ptrDSN->subflowSeqNumber, ptrDSN->dataLevelLength);

        reduceCWND (sFlowIdx);
        SetReTxTimeout (sFlowIdx); // reset RTO

        // ptrDSN->dupAckCount   = 0;
        // we retransmit only one lost pkt
        Ptr<Packet> pkt = new Packet (ptrDSN->packet, ptrDSN->dataLevelLength);
        MpTcpHeader header;
        header.SetSourcePort      (sFlow->sPort);
        header.SetDestinationPort (sFlow->dPort);
        header.SetFlags           (TcpHeader::ACK);
        header.SetSequenceNumber  (ptrDSN->subflowSeqNumber);
        header.SetAckNumber       (sFlow->RxSeqNumber);       // for the acknowledgement, we ack the sFlow last received data
        header.SetWindowSize      (AdvertisedWindowSize());
        // save the seq number of the sent data
        uint8_t hlen = 5;
        uint8_t olen = 15;
        uint8_t plen = 0;

        header.AddOptDSN (OPT_DSN, ptrDSN->dataSeqNumber, ptrDSN->dataLevelLength, ptrDSN->subflowSeqNumber);

        NS_LOG_WARN (Simulator::Now().GetSeconds() <<" RetransmitSegment -> "<< " localToken "<< localToken<<" Subflow "<<(int) sFlowIdx<<" DataSeq "<< ptrDSN->dataSeqNumber <<" SubflowSeq " << ptrDSN->subflowSeqNumber <<" dataLength " << ptrDSN->dataLevelLength << " packet size " << pkt->GetSize() << " 3DupACK");

        switch ( AlgoPR )
        {
            case Eifel:
                if(ptrDSN->retransmited == false)
                {
                    ptrDSN->retransmited = true;
                    ptrDSN->tsval = Simulator::Now ().GetMilliSeconds (); // update timestamp value to the current time
                }
                header.AddOptTT  (OPT_TT, ptrDSN->tsval, 0);
                olen += 17;
                break;
            case D_SACK:
                if(ptrDSN->retransmited == false)
                {
                    ptrDSN->retransmited = true;
                    retransSeg[ptrDSN->dataSeqNumber] = ptrDSN->dataLevelLength;
                }
                break;
            default:
                break;
        }

        plen = (4 - (olen % 4)) % 4;
        olen = (olen + plen) / 4;
        hlen += olen;
        header.SetLength(hlen);
        header.SetOptionsLength(olen);
        header.SetPaddingLength(plen);
        m_mptcp->SendPacket (pkt, header, sFlow->sAddr, sFlow->dAddr);
        // Notify the application of the data being sent

    }else if ( ptrDSN->dupAckCount > 3 )
    {
    }
    NS_LOG_LOGIC ("leaving DupAck");
}

void
MpTcpSocketImpl::GenerateRTTPlot ()
{
    //NS_LOG_FUNCTION_NOARGS ();

    if ( subflows[0]->measuredRTT.size() == 0)
        return;

    std::ofstream outfile ("rtt-cdf.plt");

    Gnuplot rttGraph = Gnuplot ("rtt-cdf.png", "RTT Cumulative Distribution Function");
    rttGraph.SetLegend("RTT (s)", "CDF");
    rttGraph.SetTerminal ("png");//postscript eps color enh \"Times-BoldItalic\"");
    rttGraph.SetExtra  ("set yrange [0:1.5]");

    for(uint16_t idx = 0; idx < subflows.size(); idx++)
    {
        MpTcpSubFlow * sFlow = subflows[idx];
        Time rtt = sFlow->rtt->Estimate ();
        NS_LOG_LOGIC("saddr = " << sFlow->sAddr << ", dAddr = " << sFlow->dAddr);
        double cdf      = 0.0;
        int    dupCount = 1;
        int    totCount = sFlow->measuredRTT.size();

        if (totCount == 0)
            continue;

        NS_LOG_LOGIC("Estimated RTT for subflow[ "<<idx<<" ] = " << rtt.GetMilliSeconds() << " ms");
        Gnuplot2dDataset dataSet;
        dataSet.SetStyle (Gnuplot2dDataset::LINES_POINTS);
        std::stringstream title;
        title << "Subflow " << idx;
        dataSet.SetTitle (title.str());

        multiset<double>::iterator it = sFlow->measuredRTT.begin();
        //list<double>::iterator it = sFlow->measuredRTT.begin();
        double previous = *it;

        for (it++; it != sFlow->measuredRTT.end(); it++)
        {
            NS_LOG_LOGIC("MpTcpSocketImpl::GenerateRTTPlot -> rtt["<<idx<<"] = "<< previous);
            if( previous == *it )
            {
                dupCount++;
            }else
            {
                cdf += (double) dupCount / (double) totCount;
                dataSet.Add (previous, cdf);
                dupCount = 1;
                previous = *it;
            }
        }
        cdf += (double) dupCount / (double) totCount;
        dataSet.Add (previous, cdf);

        rttGraph.AddDataset (dataSet);
    }
    //rttGraph.SetTerminal ("postscript eps color enh \"Times-BoldItalic\"");
    rttGraph.GenerateOutput (outfile);
    outfile.close();
}

bool
MpTcpSocketImpl::StoreUnOrderedData (DSNMapping *ptr1)
{
    //NS_LOG_FUNCTION (this);
    /**
    * return the statement depending on successfully inserting or not the data
    * if unOrdered buffer can't hold the out of sequence data and currently received
    */
    bool inserted = false;
    for(list<DSNMapping *>::iterator it = unOrdered.begin(); it != unOrdered.end(); ++it)
    {
        DSNMapping *ptr2 = *it;
        if(ptr1->dataSeqNumber == ptr2->dataSeqNumber)
        {
            NS_LOG_INFO ("Data Sequence ("<< ptr1->dataSeqNumber <<") already stored in unOrdered buffer !");
            return false;
        }
        if(ptr1->dataSeqNumber < ptr2->dataSeqNumber)
        {
            unOrdered.insert(it, ptr1);
            inserted = true;
            break;
        }
    }
    if ( !inserted )
        unOrdered.insert (unOrdered.end(), ptr1);

    return true;
}

int
MpTcpSocketImpl::Close (void)
{
  NS_LOG_LOGIC("MpTcpSocketImpl" << m_node->GetId() << "::Close() -> Number of subflows = " << subflows.size());
  // First we check to see if there is any unread rx data
  // Bug number 426 claims we should send reset in this case.

    GenerateRTTPlot();

    NS_LOG_INFO("///////////////////////////////////////////////////////////////////////////////");
    NS_LOG_INFO("Closing subflows");
    for(uint16_t idx = 0; idx < subflows.size(); idx++)
    {
        if( subflows[idx]->state != CLOSED )
        {
            NS_LOG_INFO("Subflow " << idx);
            NS_LOG_INFO("TxSeqNumber = " << subflows[idx]->TxSeqNumber);
            NS_LOG_INFO("RxSeqNumber = " << subflows[idx]->RxSeqNumber);
            NS_LOG_INFO("highestAck  = " << subflows[idx]->highestAck);
            NS_LOG_INFO("maxSeqNb    = " << subflows[idx]->maxSeqNb);
            ProcessAction (idx, ProcessEvent (idx, APP_CLOSE) );
        }
    }
    NS_LOG_INFO("///////////////////////////////////////////////////////////////////////////////");
  return 0;
}

bool
MpTcpSocketImpl::CloseMultipathConnection ()
{
    NS_LOG_FUNCTION_NOARGS();
    bool closed  = false;
    uint32_t cpt = 0;
    for(uint32_t i = 0; i < subflows.size(); i++)
    {
        NS_LOG_LOGIC("Subflow (" << i << ") TxSeqNumber (" << subflows[i]->TxSeqNumber << ") RxSeqNumber = " << subflows[i]->RxSeqNumber);
        NS_LOG_LOGIC("highestAck (" << subflows[i]->highestAck << ") maxSeqNb    = " << subflows[i]->maxSeqNb);

        if( subflows[i]->state == CLOSED )
            cpt++;
    }
    if( cpt == subflows.size() )
        NotifyNormalClose();
    return closed;
}

bool
MpTcpSocketImpl::isMultipath ()
{
    return mpEnabled;
}

void
MpTcpSocketImpl::AdvertiseAvailableAddresses ()
{
    //NS_LOG_FUNCTION_NOARGS();
  if(mpEnabled == true)
  {
    // there is at least one subflow
    MpTcpSubFlow * sFlow = subflows[0];
    mpSendState = MP_ADDR;
    MpTcpAddressInfo * addrInfo;
    Ptr<Packet> pkt = new Packet(0);//Create<Packet> ();
    MpTcpHeader header;
    header.SetFlags           (TcpHeader::ACK);//flags);
    header.SetSequenceNumber  (sFlow->TxSeqNumber);
    header.SetAckNumber       (sFlow->RxSeqNumber);
    header.SetSourcePort      (m_endPoint->GetLocalPort ());
    header.SetDestinationPort (m_remotePort);

    uint8_t hlen = 0;
    uint8_t olen = 0;

    Ptr<Ipv4L3Protocol> ipv4 = m_node->GetObject<Ipv4L3Protocol> ();

    for(uint32_t i = 0; i < ipv4->GetNInterfaces(); i++)
    {
        //Ptr<NetDevice> device = m_node->GetDevice(i);
        Ptr<Ipv4Interface> interface = ipv4->GetInterface(i);
        Ipv4InterfaceAddress interfaceAddr = interface->GetAddress (0);
        // do not consider loopback addresses
        if(interfaceAddr.GetLocal() == Ipv4Address::GetLoopback())
            continue;

        addrInfo = new MpTcpAddressInfo();
        addrInfo->addrID   = i;
        addrInfo->ipv4Addr = interfaceAddr.GetLocal();
        addrInfo->mask     = interfaceAddr.GetMask ();

        //addrInfo->ipv4Addr = Ipv4Address::ConvertFrom(device->GetAddress());
//NS_LOG_INFO("MpTcpSocketImpl::AdvertiseAvailableAddresses -> Ipv4 addresse = "<< addrInfo->ipv4Addr);

        header.AddOptADDR(OPT_ADDR, addrInfo->addrID, addrInfo->ipv4Addr);
        olen += 6;
        localAddrs.insert(localAddrs.end(), addrInfo);
    }
    uint8_t plen = (4 - (olen % 4)) % 4;
//NS_LOG_INFO("MpTcpSocketImpl::AdvertiseAvailableAddresses -> number of addresses " << localAddrs.size());
    header.SetWindowSize (AdvertisedWindowSize());
    // urgent pointer
    // check sum filed
    olen = (olen + plen) / 4;
    hlen = 5 + olen;
    header.SetLength(hlen);
    header.SetOptionsLength(olen);
    header.SetPaddingLength(plen);

    //SetReTxTimeout (0);

    m_mptcp->SendPacket (pkt, header, m_endPoint->GetLocalAddress (), m_remoteAddress);
    sFlow->TxSeqNumber ++;
    sFlow->maxSeqNb++;
  }
}

uint32_t
MpTcpSocketImpl::GetOutputInf (Ipv4Address addr)
{
    uint32_t oif = 0;
    Ptr<Ipv4L3Protocol> ipv4 = m_node->GetObject<Ipv4L3Protocol> ();
    for(uint32_t i = 0; i < localAddrs.size(); i++)
    {
        MpTcpAddressInfo* inf = localAddrs[i];

        if(addr == inf->ipv4Addr)
        {
            oif = inf->addrID;
            break;
        }
    }

    return oif;
}

bool
MpTcpSocketImpl::IsThereRoute (Ipv4Address src, Ipv4Address dst)
{
    //NS_LOG_FUNCTION(this << src << dst);
    bool found = false;
    // Look up the source address
    Ptr<Ipv4> ipv4 = m_node->GetObject<Ipv4> ();
    if (ipv4->GetRoutingProtocol () != 0)
    {
        Ipv4Header l3Header;
        Socket::SocketErrno errno_;
        Ptr<Ipv4Route> route;
        uint32_t oif = GetOutputInf (src); //specify non-zero if bound to a source address
        l3Header.SetSource (src);
        l3Header.SetDestination (dst);
        route = ipv4->GetRoutingProtocol ()->RouteOutput (Ptr<Packet> (), l3Header, oif, errno_);

        if ((route != 0) && (src == route->GetSource ()))
        {
            NS_LOG_INFO ("IsThereRoute -> Route from srcAddr "<< src << " to dstAddr " << dst << " oit "<<oif<<", exist !");
            found = true;
        }else
            NS_LOG_INFO ("IsThereRoute -> No Route from srcAddr "<< src << " to dstAddr " << dst << " oit "<<oif<<", exist !");
    }
    return found;
}

bool
MpTcpSocketImpl::IsLocalAddress (Ipv4Address addr)
{
    //NS_LOG_FUNCTION(this << addr);
    bool found = false;
    MpTcpAddressInfo * pAddrInfo;
    for(uint32_t i = 0; i < localAddrs.size(); i++)
    {
        pAddrInfo = localAddrs[i];
        if( pAddrInfo->ipv4Addr == addr)
        {
            found = true;
            break;
        }
    }
    return found;
}

void
MpTcpSocketImpl::DetectLocalAddresses ()
{
    //NS_LOG_FUNCTION_NOARGS();
    MpTcpAddressInfo * addrInfo;
    Ptr<Ipv4L3Protocol> ipv4 = m_node->GetObject<Ipv4L3Protocol> ();

    for(uint32_t i = 0; i < ipv4->GetNInterfaces(); i++)
    {
        //Ptr<NetDevice> device = m_node->GetDevice(i);
        Ptr<Ipv4Interface> interface = ipv4->GetInterface(i);
        Ipv4InterfaceAddress interfaceAddr = interface->GetAddress (0);
        // do not consider loopback addresses
        if( (interfaceAddr.GetLocal() == Ipv4Address::GetLoopback()) || (IsLocalAddress(interfaceAddr.GetLocal())) )
            continue;

        addrInfo = new MpTcpAddressInfo();
        addrInfo->addrID   = i;
        addrInfo->ipv4Addr = interfaceAddr.GetLocal();
        addrInfo->mask     = interfaceAddr.GetMask ();

        localAddrs.insert(localAddrs.end(), addrInfo);
    }
}

uint32_t
MpTcpSocketImpl::BytesInFlight ()
{
  //NS_LOG_FUNCTION_NOARGS ();
  return unAckedDataCount; //m_highTxMark - m_highestRxAck;
}

uint16_t
MpTcpSocketImpl::AdvertisedWindowSize ()
{
    //NS_LOG_FUNCTION_NOARGS();
    uint16_t window = 0;
/*
    if( recvingBuffer != 0 )
        window = recvingBuffer->FreeSpaceSize ();
*/
    window = 65535;
    return window;
}

uint32_t
MpTcpSocketImpl::AvailableWindow (uint8_t sFlowIdx)
{
  //NS_LOG_FUNCTION_NOARGS ();
  MpTcpSubFlow * sFlow = subflows[sFlowIdx];
  uint32_t window   = std::min ( remoteRecvWnd, static_cast<uint32_t> (sFlow->cwnd) ) * sFlow->MSS;
  uint32_t unAcked  = sFlow->maxSeqNb - sFlow->highestAck;
  NS_LOG_LOGIC("Subflow ("<< (int)sFlowIdx <<"): AvailableWindow -> remoteRecvWnd  = " << remoteRecvWnd <<" unAckedDataCnt = " << unAcked <<" CWND in bytes  = " << sFlow->cwnd * sFlow->MSS);
  if (window < unAcked) //DataCount)
  {
      NS_LOG_LOGIC("MpTcpSocketImpl::AvailableWindow -> Available Tx window is 0");
      return 0;  // No space available
  }
  return (window - unAcked);//DataCount);       // Amount of window space available
}

uint32_t
MpTcpSocketImpl::GetTxAvailable ()
{
    //NS_LOG_FUNCTION_NOARGS();
    //NS_LOG_INFO ("sendingBuffer->FreeSpaceSize () == " << sendingBuffer->FreeSpaceSize ());
    return sendingBuffer->FreeSpaceSize ();
}

void
MpTcpSocketImpl::SetSourceAddress (Ipv4Address src)
{
    //NS_LOG_FUNCTION_NOARGS();
    m_localAddress = src;
    if(m_endPoint != 0)
    {
        m_endPoint->SetLocalAddress(src);
    }
}

Ipv4Address
MpTcpSocketImpl::GetSourceAddress ()
{
    //NS_LOG_FUNCTION_NOARGS();
    return m_localAddress;
}

uint8_t
MpTcpSocketImpl::LookupByAddrs (Ipv4Address src, Ipv4Address dst)
{
    //NS_LOG_FUNCTION_NOARGS();
    MpTcpSubFlow *sFlow = 0;
    uint8_t sFlowIdx = MaxSubFlowNumber;

    if( IsThereRoute (src, dst)==false )
    {
        // there is problem in the stated src (local) address
        for(vector<MpTcpAddressInfo *>::iterator it=localAddrs.begin(); it!=localAddrs.end(); ++it)
        {
            Ipv4Address ipv4Addr = (*it)->ipv4Addr;
            if( IsThereRoute (ipv4Addr, dst)==true )
            {
                src = ipv4Addr;
                m_localAddress  = ipv4Addr;
                break;
            }
        }
    }

    for(uint8_t i = 0; i < subflows.size(); i++)
    {
        sFlow = subflows[i];
        // on address can only participate to a one subflow, so we can find that subflow by unsing the source address or the destination, but the destination address is the correct one, so use it
        if( (sFlow->sAddr==src && sFlow->dAddr==dst) || (sFlow->dAddr==dst) )
        {
            sFlowIdx = i;
            break;
        }
    }

    if(! (sFlowIdx < MaxSubFlowNumber) )
    {
      if(m_connected == false && subflows.size()==1)
      {
          sFlowIdx = 0;
      }
      else
      {
          if( IsLocalAddress(m_localAddress) )
          {
                sFlowIdx = subflows.size();
                MpTcpSubFlow *sFlow = new MpTcpSubFlow( sfInitSeqNb[m_localAddress] + 1);
                sFlow->routeId   = subflows[subflows.size() - 1]->routeId + 1;
                sFlow->dAddr     = m_remoteAddress;
                sFlow->dPort     = m_remotePort;
                sFlow->sAddr     = m_localAddress;
                sFlow->sPort     = m_localPort;
                sFlow->MSS       = getL3MTU(m_localAddress);
                sFlow->bandwidth = getBandwidth(m_endPoint->GetLocalAddress ());
                // at its creation, the subflow take the state of the global connection
                if(m_state == LISTEN)
                    sFlow->state = m_state;
                else if(m_state == ESTABLISHED)
                    sFlow->state = SYN_SENT;
                subflows.insert(subflows.end(), sFlow);
                NS_LOG_INFO("Node ("<<m_node->GetId()<<") LookupByAddrs -> sFlowIdx " << (int) sFlowIdx <<" created: (src,dst) = (" << sFlow->sAddr << "," << sFlow->dAddr << ")" );
          }else
          {
                NS_LOG_WARN ("MpTcpSocketImpl::LookupByAddrs -> sub flow related to (src,dst) = ("<<m_endPoint->GetLocalAddress()<<","<<m_endPoint->GetPeerAddress()<<") not found !");
          }
      }
    }

    NS_LOG_INFO("Node ("<<m_node->GetId()<<") LookupByAddrs -> subflows number = " << subflows.size() <<" (src,dst) = (" << src << "," << dst << ") below to subflow " << (int) sFlowIdx );

    return sFlowIdx;
}

void
MpTcpSocketImpl::OpenCWND (uint8_t sFlowIdx, uint32_t ackedBytes)
{
    NS_LOG_FUNCTION(this << (int) sFlowIdx << ackedBytes);
    MpTcpSubFlow * sFlow = subflows[sFlowIdx];
    double   increment = 0;
    double   cwnd      = sFlow->cwnd;
    uint32_t ssthresh  = sFlow->ssthresh;
    uint32_t segSize   = sFlow->MSS;
    bool     normalCC  = true;

    if ( sFlow->phase == DSACK_SS )
    {

        if( cwnd + 1 < sFlow->savedCWND )
        {
            increment = 1;
            normalCC  = false;
            NS_LOG_WARN ("Subflow ("<< (int) sFlowIdx <<") Congestion Control (DSACK Slow Start) increment is 1 to reach "<< sFlow->savedCWND );
        }else
        {
            NS_LOG_WARN ("End of DSACK phase in subflow ("<< (int) sFlowIdx <<") Congestion Control (DSACK Slow Start) reached "<< sFlow->savedCWND );
            sFlow->savedCWND = 0;
            sFlow->phase = Congestion_Avoidance;
        }
    }else if( (sFlow->phase == RTO_Recovery) && (cwnd * segSize < ssthresh) )
    {
        increment = 1;
        normalCC  = false;
        NS_LOG_WARN (Simulator::Now().GetSeconds() <<" Subflow ("<< (int) sFlowIdx <<") Congestion Control (Slow Start Recovery) increment is 1 current cwnd "<< cwnd );
    }
    if (normalCC == true)
    {
    if( cwnd * segSize < ssthresh )
    {
        increment = 1;
        NS_LOG_ERROR ("Congestion Control (Slow Start) increment is 1");
    }else if( totalCwnd != 0 )
    {
        switch ( AlgoCC )
        {
            case RTT_Compensator:
                //increment = std::min( alpha * ackedBytes / totalCwnd, (double) ackedBytes / cwnd );
                calculateSmoothedCWND (sFlowIdx);
                calculate_alpha();
                increment = std::min( alpha / totalCwnd, 1.0 / cwnd );
                NS_LOG_ERROR ("Congestion Control (RTT_Compensator): alpha "<<alpha<<" ackedBytes (" << ackedBytes << ") totalCwnd ("<< totalCwnd<<") -> increment is "<<increment);
                break;

            case Linked_Increases:
                calculateSmoothedCWND (sFlowIdx);
                calculate_alpha();
                increment = alpha / totalCwnd;
                NS_LOG_ERROR ("Subflow "<<(int)sFlowIdx<<" Congestion Control (Linked_Increases): alpha "<<alpha<<" increment is "<<increment<<" ssthresh "<< ssthresh << " cwnd "<<cwnd );
                break;

            case Uncoupled_TCPs:
                increment = 1.0 / cwnd;
                NS_LOG_ERROR ("Subflow "<<(int)sFlowIdx<<" Congestion Control (Uncoupled_TCPs) increment is "<<increment<<" ssthresh "<< ssthresh << " cwnd "<<cwnd);
                break;

            case Fully_Coupled :
                increment = 1.0 / totalCwnd;
                NS_LOG_ERROR ("Subflow "<<(int)sFlowIdx<<" Congestion Control (Fully_Coupled) increment is "<<increment<<" ssthresh "<< ssthresh << " cwnd "<<cwnd);
                break;

            default :
                increment = 1.0 / cwnd;
                break;
        }
    }else
    {
        increment = 1 / cwnd;
        NS_LOG_ERROR ("Congestion Control (totalCwnd == 0) increment is "<<increment);
    }
    }
    if (totalCwnd + increment <= remoteRecvWnd)
        sFlow->cwnd += increment;
    double rtt = sFlow->rtt->est.GetSeconds();
    NS_LOG_WARN (Simulator::Now().GetSeconds() <<" MpTcpSocketImpl -> "<< " localToken "<< localToken<<" Subflow "<< (int)sFlowIdx <<": RTT "<< sFlow->rtt->est.GetSeconds() <<" Moving cwnd from " << cwnd << " to " << sFlow->cwnd <<" Throughput "<<(sFlow->cwnd * sFlow->MSS * 8)/rtt<< " GlobalThroughput "<<getGlobalThroughput()<< " Efficacity " <<  getConnectionEfficiency() << " delay "<<getPathDelay(sFlowIdx)<<" alpha "<< alpha <<" Sum CWND ("<< totalCwnd <<")");
}

void
MpTcpSocketImpl::calculate_alpha ()
{
    // this method is called whenever a congestion happen in order to regulate the agressivety of subflows
   NS_LOG_FUNCTION_NOARGS ();
   meanTotalCwnd = totalCwnd = alpha = 0;
   double maxi       = 0;
   double sumi       = 0;

   for (uint32_t i = 0; i < subflows.size() ; i++)
   {
       MpTcpSubFlow * sFlow = subflows[i];

       totalCwnd += sFlow->cwnd;
       meanTotalCwnd += sFlow->scwnd;

     /* use smmothed RTT */
     Time time = sFlow->rtt->Estimate ();
     double rtt = time.GetSeconds ();
     if (rtt < 0.000001)
       continue;                 // too small

     double tmpi = sFlow->scwnd / (rtt * rtt);
     if (maxi < tmpi)
       maxi = tmpi;

     sumi += sFlow->scwnd / rtt;
   }
   if (!sumi)
     return;
   alpha = meanTotalCwnd * maxi / (sumi * sumi);
   NS_LOG_ERROR ("calculate_alpha: alpha "<<alpha<<" totalCwnd ("<< meanTotalCwnd<<")");
}

void
MpTcpSocketImpl::calculateSmoothedCWND (uint8_t sFlowIdx)
{
    MpTcpSubFlow *sFlow = subflows [sFlowIdx];
    if (sFlow->scwnd < 1)
        sFlow->scwnd = sFlow->cwnd;
    else
        sFlow->scwnd = sFlow->scwnd * 0.875 + sFlow->cwnd * 0.125;
}

void
MpTcpSocketImpl::Destroy (void)
{
    NS_LOG_FUNCTION_NOARGS();
}

MpTcpSubFlow *
MpTcpSocketImpl::GetSubflow (uint8_t sFlowIdx)
{
    return subflows [sFlowIdx];
}

void
MpTcpSocketImpl::SetCongestionCtrlAlgo (CongestionCtrl_t ccalgo)
{
    AlgoCC = ccalgo;
}

void
MpTcpSocketImpl::SetDataDistribAlgo (DataDistribAlgo_t ddalgo)
{
    distribAlgo = ddalgo;
}

bool
MpTcpSocketImpl::rejectPacket(double threshold)
{
    //NS_LOG_FUNCTION_NOARGS();

    bool reject = false;
    double probability = (double) (rand() % 1013) / 1013.0;
    NS_LOG_INFO("rejectPacket -> probability == " << probability);
    if( probability < threshold )
        reject = true;

    return reject;

}

double
MpTcpSocketImpl::getPathDelay(uint8_t idxPath)
{
    TimeValue delay;
    Ptr<Ipv4L3Protocol> ipv4 = m_node->GetObject<Ipv4L3Protocol> ();
    // interface 0 is the loopback interface
    Ptr<Ipv4Interface> interface = ipv4->GetInterface(idxPath + 1);
    Ipv4InterfaceAddress interfaceAddr = interface->GetAddress (0);
    // do not consider loopback addresses
    if(interfaceAddr.GetLocal() == Ipv4Address::GetLoopback())
        return 0.0;
    Ptr<NetDevice> netDev =  interface->GetDevice();
    Ptr<Channel> P2Plink  =  netDev->GetChannel();
    P2Plink->GetAttribute(string("Delay"), delay);
    return delay.Get().GetSeconds();
}

uint64_t
MpTcpSocketImpl::getPathBandwidth(uint8_t idxPath)
{
    StringValue str;
    Ptr<Ipv4L3Protocol> ipv4 = m_node->GetObject<Ipv4L3Protocol> ();
    // interface 0 is the loopback interface
    Ptr<Ipv4Interface> interface = ipv4->GetInterface(idxPath + 1);
    Ipv4InterfaceAddress interfaceAddr = interface->GetAddress (0);
    // do not consider loopback addresses
    if(interfaceAddr.GetLocal() == Ipv4Address::GetLoopback())
        return 0.0;
    Ptr<NetDevice> netDev =  interface->GetDevice();

    if( netDev->IsPointToPoint() == true )
    {
        netDev->GetAttribute(string("DataRate"), str);
    }else
    {
        Ptr<Channel> link  =  netDev->GetChannel();
        link->GetAttribute(string("DataRate"), str);
    }

    DataRate bandwidth (str.Get());
    return bandwidth.GetBitRate ();
}

double
MpTcpSocketImpl::getGlobalThroughput()
{
    double gThroughput = 0;
    for(uint32_t i=0; i< subflows.size(); i++)
    {
        MpTcpSubFlow* sFlow = subflows[i];
        gThroughput += (sFlow->cwnd * sFlow->MSS * 8) / sFlow->rtt->est.GetSeconds();
    }
    return gThroughput;
}

double
MpTcpSocketImpl::getConnectionEfficiency()
{
    double gThroughput =0.0;
    uint64_t gBandwidth = 0;
    for(uint32_t i = 0; i < subflows.size(); i++)
    {
        MpTcpSubFlow* sFlow = subflows[i];
        gThroughput += (sFlow->cwnd * sFlow->MSS * 8) / sFlow->rtt->est.GetSeconds();
        gBandwidth += getPathBandwidth(i);
    }
    return gThroughput / gBandwidth;
}

DSNMapping*
MpTcpSocketImpl::getAckedSegment(uint64_t lEdge, uint64_t rEdge)
{
    for(uint8_t i = 0; i < subflows.size(); i++)
    {
        MpTcpSubFlow* sFlow = subflows[i];
        for (list<DSNMapping *>::iterator it = sFlow->mapDSN.begin(); it != sFlow->mapDSN.end(); ++it)
        {
            DSNMapping* dsn = *it;
            if(dsn->dataSeqNumber == lEdge && dsn->dataSeqNumber + dsn->dataLevelLength == rEdge)
            {
                return dsn;
            }
        }
    }
    return 0;
}


/*
bool
MpTcpSocketImpl::IsRetransmitted (uint64_t leftEdge, uint64_t rightEdge)
{
    bool retransmitted = false;

    for (uint8_t i = 0; i < subflows.size(); i++)
    {
        MpTcpSubFlow *sFlow = subflows[i];
        list<DSNMapping *>::iterator current = sFlow->mapDSN.begin();
        list<DSNMapping *>::iterator next = sFlow->mapDSN.begin();
        while( current != sFlow->mapDSN.end() )
        {
            ++next;
            DSNMapping *ptrDSN = *current;
            if ( (ptrDSN->dataSeqNumber >= leftEdge) && (ptrDSN->dataSeqNumber + ptrDSN->dataLevelLength <= rightEdge) )
            {
                // By checking the data level sequence number in the received TCP header option
                // we can find if the segment has already been retransmitted or not
                retransmitted = ptrDSN->retransmited;
            }
            if ( retransmitted == true )
            {
                NS_LOG_WARN("Segement between seq n°"<< leftEdge <<" and "<< rightEdge <<" retransmitted !");
                break;
            }
            current = next;
        }
    }
    return retransmitted;
}
*/

}//namespace ns3


