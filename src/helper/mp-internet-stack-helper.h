#ifndef MP_INTERNET_STACK_HELPER_H
#define MP_INTERNET_STACK_HELPER_H

#include "node-container.h"
#include "net-device-container.h"
#include "ns3/packet.h"
#include "ns3/ptr.h"
#include "ns3/object-factory.h"
#include "ns3/pcap-writer.h"
#include "ns3/ascii-writer.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/ipv6-l3-protocol.h"

namespace ns3 {

class Node;
class Ipv4RoutingHelper;
class Ipv6RoutingHelper;

/**
 * \brief aggregate IP/TCP/UDP functionality to existing Nodes.
 */
class MpInternetStackHelper
{
public:
  MpInternetStackHelper(void);
  ~MpInternetStackHelper(void);
  void CreateAndAggregateObjectFromTypeId (Ptr<Node> node, const std::string typeId);
  void Install(NodeContainer nodes);


private:
    Ipv4RoutingHelper *m_routing;

};

} // namespace ns3

#endif /* MP_INTERNET_STACK_HELPER_H */
