#include "ns3/assert.h"
#include "ns3/log.h"
#include "ns3/nstime.h"
#include "ns3/boolean.h"
#include "ns3/object-vector.h"

#include "ns3/packet.h"
#include "ns3/node.h"
#include "ns3/ipv4-route.h"

#include "mp-tcp-l4-protocol.h"
#include "tcp-header.h"
#include "ipv4-end-point-demux.h"
#include "ipv4-end-point.h"
#include "ipv4-l3-protocol.h"
#include "tcp-socket-factory-impl.h"
#include "tcp-socket-impl.h"
#include "rtt-estimator.h"
#include "tcp-typedefs.h"

#include <vector>
#include <list>
#include <sstream>
#include <iomanip>

NS_LOG_COMPONENT_DEFINE ("MpTcpL4Protocol");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (MpTcpL4Protocol);

const uint8_t MpTcpL4Protocol::PROT_NUMBER = 6;


TypeId
MpTcpL4Protocol::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::MpTcpL4Protocol")
    .SetParent<TcpL4Protocol> ()
    .AddConstructor<MpTcpL4Protocol> ()
    ;
  return tid;
}

MpTcpL4Protocol::MpTcpL4Protocol ()
//  : m_endPoints (new Ipv4EndPointDemux ())
{
  NS_LOG_FUNCTION_NOARGS ();
  NS_LOG_LOGIC("Made a MpTcpL4Protocol "<<this);
  m_endPoints = new Ipv4EndPointDemux ();
}

MpTcpL4Protocol::~MpTcpL4Protocol ()
{
  NS_LOG_FUNCTION_NOARGS ();
}
int
MpTcpL4Protocol::GetProtocolNumber (void) const
{
  return PROT_NUMBER;
}

void
MpTcpL4Protocol::NotifyNewAggregate ()
{
    NS_LOG_FUNCTION_NOARGS();
  if (m_node == 0)
    {
      Ptr<Node> node = this->GetObject<Node> ();
      if (node != 0)
        {
          Ptr<Ipv4L3Protocol> ipv4 = this->GetObject<Ipv4L3Protocol> ();
          if (ipv4 != 0)
            {
              this->SetNode (node);
              ipv4->Insert (this);
              Ptr<TcpSocketFactoryImpl> tcpFactory = CreateObject<TcpSocketFactoryImpl> ();
              tcpFactory->SetTcp (this);
              node->AggregateObject (tcpFactory);
            }
        }
    }
  Object::NotifyNewAggregate ();
}

enum Ipv4L4Protocol::RxStatus
MpTcpL4Protocol::Receive (Ptr<Packet> packet, Ipv4Address const &source,
                          Ipv4Address const &destination, Ptr<Ipv4Interface> incomingInterface)
{
  NS_LOG_FUNCTION (this << packet << source << destination << incomingInterface);
  MpTcpHeader mptcpHeader;
  /*if(Node::ChecksumEnabled ())
  {
    mptcpHeader.EnableChecksums();
    mptcpHeader.InitializeChecksum (source, destination, PROT_NUMBER);
  }
*/
  packet->PeekHeader (mptcpHeader);
  uint16_t srcPort = mptcpHeader.GetSourcePort ();
  uint16_t dstPort = mptcpHeader.GetDestinationPort ();

  NS_LOG_LOGIC("MpTcpL4Protocol " << this << " receiving seq " << mptcpHeader.GetSequenceNumber() << " ack " << mptcpHeader.GetAckNumber() << " flags "<< std::hex << (int)mptcpHeader.GetFlags() << std::dec << " data size " << packet->GetSize());

/*
  if(!tcpHeader.IsChecksumOk ())
  {
    NS_LOG_INFO("Bad checksum, dropping packet!");
    return Ipv4L4Protocol::RX_CSUM_FAILED;
  }
*/
  NS_LOG_LOGIC ("MpTcpL4Protocol "<<this<<" received a packet");
  Ipv4EndPointDemux::EndPoints endPoints = m_endPoints->Lookup (destination, dstPort, source, srcPort,incomingInterface);
  if (endPoints.empty ())
  {
      // trying with destination port
      NS_LOG_LOGIC ("MpTcpL4Protocol:Receive() ->  Trying to look up for end point with destination port: "<< dstPort);
      Ipv4EndPointDemux::EndPoints allEndPoints = m_endPoints->GetAllEndPoints();
      Ipv4EndPointDemux::EndPoints::iterator it;
      while( allEndPoints.size() > 0)
      {
          it = allEndPoints.begin();
          uint16_t localPort = (*it)->GetLocalPort ();
          if(localPort != dstPort)
          {
            it = allEndPoints.erase(it);
          }
          else
          {
            NS_LOG_LOGIC ("MpTcpL4Protocol:Receive() ->  related end port for destination port ("<< dstPort << ") is found");
            break;
          }
      }

      (*allEndPoints.begin ())->SetLocalAddress ( destination );
      (*allEndPoints.begin ())->SetPeer (source, srcPort);
      (*allEndPoints.begin ())->ForwardUp (packet, source, srcPort);
NS_LOG_INFO ("MpTcpL4Protocol (endPoint is empty) forwarding up to endpoint/socket dest = " << destination<<" dstPort "<<dstPort<<" src = " << source);

      NS_LOG_INFO ("MpTcpL4Protocol -> leaving Receive");
      return Ipv4L4Protocol::RX_OK;
  }

  if (endPoints.empty ())
  {
    NS_LOG_LOGIC ("  No endpoints matched on MpTcpL4Protocol "<<this);
    std::ostringstream oss;
    oss<<"  destination IP: ";
    destination.Print (oss);
    oss<<" destination port: "<< dstPort <<" source IP: ";
    source.Print (oss);
    oss<<" source port: "<< srcPort;
    NS_LOG_LOGIC (oss.str ());
/*
    if (!(tcpHeader.GetFlags () & TcpHeader::RST))
      {
        // build a RST packet and send
        Ptr<Packet> rstPacket = Create<Packet> ();
        TcpHeader header;
        if (tcpHeader.GetFlags () & TcpHeader::ACK)
          {
            // ACK bit was set
            header.SetFlags (TcpHeader::RST);
            header.SetSequenceNumber (header.GetAckNumber ());
          }
        else
          {
            header.SetFlags (TcpHeader::RST | TcpHeader::ACK);
            header.SetSequenceNumber (SequenceNumber (0));
            header.SetAckNumber (header.GetSequenceNumber () + SequenceNumber (1));
          }
        header.SetSourcePort (tcpHeader.GetDestinationPort ());
        header.SetDestinationPort (tcpHeader.GetSourcePort ());
        SendPacket (rstPacket, header, destination, source);
        return Ipv4L4Protocol::RX_ENDPOINT_CLOSED;
      }
    else
      {
        return Ipv4L4Protocol::RX_ENDPOINT_CLOSED;
      }
*/
  }
  NS_ASSERT_MSG (endPoints.size() == 1 , "Demux returned more than one endpoint");
NS_LOG_INFO ("MpTcpL4Protocol (endPoint not empty) forwarding up to endpoint/socket dest = " << destination<<" dstPort "<<dstPort<<" src = " << source);

  (*endPoints.begin ())->SetLocalAddress ( destination );
  (*endPoints.begin ())->SetPeer (source, srcPort);
  (*endPoints.begin ())->ForwardUp (packet, source, srcPort);

  return Ipv4L4Protocol::RX_OK;

}

void
MpTcpL4Protocol::DoDispose(void)
{
  NS_LOG_FUNCTION_NOARGS ();
  /*for (std::vector<Ptr<TcpSocketImpl> >::iterator i = m_sockets.begin (); i != m_sockets.end (); i++)
    {
      *i = 0;
    }
  m_sockets.clear ();
*/
  if (m_endPoints != 0)
    {
      delete m_endPoints;
      m_endPoints = 0;
    }

  m_node = 0;
  Ipv4L4Protocol::DoDispose ();
}

void
MpTcpL4Protocol::SendPacket (Ptr<Packet> p, MpTcpHeader l4Header, Ipv4Address src, Ipv4Address dst)
{
    NS_LOG_LOGIC("MpTcpL4Protocol " << this ); /*
              << " sending seq " << l4Header.GetSequenceNumber()
              << " ack " << l4Header.GetAckNumber()
              << " flags " << std::hex << (int)header.GetFlags() << std::dec
              << " data size " << p->GetSize());*/
    NS_LOG_FUNCTION (this << p << src << dst);
    //Ptr<MpTcpHeader> ptrHeader = CopyObject<MpTcpHeader>(l4Header);
    p->AddHeader (l4Header);
NS_LOG_INFO ("MpTcpL4Protocol::SendPacket -> header added successfully !");
    Ptr<Ipv4L3Protocol> ipv4 = m_node->GetObject<Ipv4L3Protocol> ();
    if (ipv4 != 0)
    {
      // XXX We've already performed the route lookup in TcpSocketImpl
      // should be cached.
      Ipv4Header l3Header;
      l3Header.SetDestination (dst);
      Socket::SocketErrno errno_;
      Ptr<Ipv4Route> route;
      uint32_t oif = 0; //specify non-zero if bound to a source address
      // use oif to specify the interface output (see Ipv4RoutingProtocol class)
      route = ipv4->GetRoutingProtocol ()->RouteOutput (p, l3Header, oif, errno_);
      NS_LOG_INFO ("MpTcpL4Protocol::SendPacket -> packet size:" << p->GetSize() <<" sAddr " << src<<" dstAddr " << dst);
      NS_LOG_INFO ("MpTcpL4Protocol::SendPacket -> Protocol n°:" << (int)PROT_NUMBER);
      NS_LOG_INFO ("MpTcpL4Protocol::SendPacket -> route      :" << route);
      ipv4->Send (p, src, dst, PROT_NUMBER, route);
      NS_LOG_INFO ("MpTcpL4Protocol::SendPacket -> leaving !");
    }
    else
      NS_FATAL_ERROR("Trying to use MpTcp on a node without an Ipv4 interface");

}
}; // namespace ns3


