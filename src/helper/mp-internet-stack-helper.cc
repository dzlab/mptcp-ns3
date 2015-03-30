#include "ns3/assert.h"
#include "ns3/log.h"
#include "ns3/object.h"
#include "ns3/names.h"
#include "ns3/ipv4.h"
#include "ns3/ipv6.h"
#include "ns3/packet-socket-factory.h"
#include "ns3/config.h"
#include "ns3/simulator.h"
#include "ns3/string.h"
#include "ns3/net-device.h"
#include "ns3/callback.h"
#include "ns3/node.h"
#include "ns3/core-config.h"
#include "ns3/pcap-writer.h"
#include "ns3/ascii-writer.h"
#include "mp-internet-stack-helper.h"
#include "ipv4-list-routing-helper.h"
#include "ipv4-static-routing-helper.h"
#include "ipv4-global-routing-helper.h"
#include "ipv6-list-routing-helper.h"
#include "ipv6-static-routing-helper.h"
#include <limits>

namespace ns3 {


MpInternetStackHelper::MpInternetStackHelper ()
  : m_routing (0)
{
  Ipv4StaticRoutingHelper staticRouting;
  Ipv4GlobalRoutingHelper globalRouting;
  Ipv4ListRoutingHelper listRouting;
  Ipv6ListRoutingHelper listRoutingv6;
  Ipv6StaticRoutingHelper staticRoutingv6;
  listRouting.Add (staticRouting, 0);
  listRouting.Add (globalRouting, -10);
  m_routing = listRouting.Copy ();
}

MpInternetStackHelper::~MpInternetStackHelper ()
{

}

void
MpInternetStackHelper::CreateAndAggregateObjectFromTypeId (Ptr<Node> node, const std::string typeId)
{
  ObjectFactory factory;
  factory.SetTypeId (typeId);
  Ptr<Object> protocol = factory.Create <Object> ();
  node->AggregateObject (protocol);
}

void
MpInternetStackHelper::Install(NodeContainer nodes)
{
    Ptr<Node> node;
    ObjectFactory m_tcpFactory;
    ObjectFactory m_mptcpFactory;
    m_tcpFactory.SetTypeId ("ns3::TcpL4Protocol");
    m_mptcpFactory.SetTypeId ("ns3::MpTcpL4Protocol");


    for (NodeContainer::Iterator i = nodes.Begin (); i != nodes.End (); ++i)
    {
        node = (*i);
        if (node->GetObject<Ipv4> () != 0)
        {
          NS_FATAL_ERROR ("MpInternetStackHelper::Install (): Aggregating "
                          "an InternetStack to a node with an existing Ipv4 object");
          return;
        }
        CreateAndAggregateObjectFromTypeId (node, "ns3::ArpL3Protocol");
        CreateAndAggregateObjectFromTypeId (node, "ns3::Ipv4L3Protocol");
        //CreateAndAggregateObjectFromTypeId (node, "ns3::MpTcpL4Protocol");
        CreateAndAggregateObjectFromTypeId (node, "ns3::Icmpv4L4Protocol");
        CreateAndAggregateObjectFromTypeId (node, "ns3::UdpL4Protocol");

        //node->AggregateObject (m_tcpFactory.Create<Object> ());
        node->AggregateObject (m_mptcpFactory.Create<Object> ());
        Ptr<PacketSocketFactory> factory = CreateObject<PacketSocketFactory> ();
        node->AggregateObject (factory);

              // Set routing
        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
        Ptr<Ipv4RoutingProtocol> ipv4Routing = m_routing->Create (node);
        ipv4->SetRoutingProtocol (ipv4Routing);

    }
}


} // namespace ns3
