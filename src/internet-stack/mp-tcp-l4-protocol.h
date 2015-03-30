#ifndef MP_TCP_L4_PROTOCOL_H
#define MP_TCP_L4_PROTOCOL_H



#include <stdint.h>
#include <vector>


#include "ns3/packet.h"
#include "ns3/ipv4-address.h"
#include "ns3/ptr.h"
#include "ns3/object-factory.h"
#include "ipv4-l4-protocol.h"
#include "tcp-l4-protocol.h"
#include "tcp-typedefs.h"
#include "ns3/ipv4-end-point-demux.h"
#include "mp-tcp-header.h"
#include "mp-tcp-socket-impl.h"


namespace ns3 {

class MpTcpL4Protocol : public TcpL4Protocol {

public:
  static const uint8_t PROT_NUMBER;
  static TypeId GetTypeId (void);

  MpTcpL4Protocol ();
  virtual ~MpTcpL4Protocol ();

  virtual int GetProtocolNumber (void) const;
  virtual enum Ipv4L4Protocol::RxStatus Receive (Ptr<Packet> packet, Ipv4Address const &source,
                                                 Ipv4Address const &destination, Ptr<Ipv4Interface> incomingInterface);
  void SendPacket (Ptr<Packet> p, MpTcpHeader l4Header, Ipv4Address src, Ipv4Address dst);

protected:
  virtual void DoDispose (void);

  virtual void NotifyNewAggregate ();

  //std::vector<Ptr<MpTcpSocketImpl> > m_sockets;


};

}; // namespace ns3


#endif /* MP_TCP_L4_PROTOCOL_H */
