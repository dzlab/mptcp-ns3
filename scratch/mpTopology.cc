#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "ns3/core-module.h"
#include "ns3/simulator-module.h"
#include "ns3/node-module.h"
#include "ns3/helper-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"

#include "ns3/mp-internet-stack-helper.h"
#include "ns3/mp-tcp-packet-sink.h"
#include "ns3/mp-tcp-l4-protocol.h"
#include "ns3/mp-tcp-socket-impl.h"
#include "ns3/point-to-point-channel.h"
#include "ns3/point-to-point-net-device.h"

/* Multipath Network Topology

     lan 10.1.1.0
      ___________
     /           \
   n1             n2
     \___________/

     lan 10.1.2.0
*/

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("FirstMultipathToplogy");

// The number of bytes to send in this simulation.
static const uint32_t totalTxBytes = 10000000;
static const uint32_t sendBufSize  = 14000; //2000000;
static const uint32_t recvBufSize  = 2000; //2000000;
static uint32_t currentTxBytes     = 0;
static const double simDuration    = 360000000.0;

Ptr<Node> client;
Ptr<Node> server;


// Perform series of 1040 byte writes (this is a multiple of 26 since
// we want to detect data splicing in the output stream)
static const uint32_t writeSize = sendBufSize;
uint8_t data[totalTxBytes];
Ptr<MpTcpSocketImpl> lSocket    = 0;
/*
PointToPointHelper fstP2Plink;
PointToPointHelper sndP2Plink;
PointToPointHelper trdP2Plink;
*/

void StartFlow (Ptr<MpTcpSocketImpl>, Ipv4Address, uint16_t);
void WriteUntilBufferFull (Ptr<Socket>, unsigned int);
void connectionSucceeded(Ptr<Socket>);
void connectionFailed(Ptr<Socket>);

void HandlePeerClose (Ptr<Socket>);
void HandlePeerError (Ptr<Socket>);
void CloseConnection (Ptr<Socket>);

int connect(Address &addr);
void variateDelay(PointToPointHelper P2Plink);

static void
CwndTracer (double oldval, double newval)
{
  NS_LOG_INFO ("Moving cwnd from " << oldval << " to " << newval);
}


int main(int argc, char *argv[])
{
    bool verbose;
    CongestionCtrl_t cc = Fully_Coupled;
    PacketReorder_t  pr = D_SACK;
    int arg1 = -1, arg2 = -1, arg3 = -1, arg4 = -1;
    int sf = 2; // number of subflows
    CommandLine cmd;
    cmd.AddValue("verbose", "Tell application to log if true", verbose);
    cmd.AddValue("level", "Tell application which log level to use:\n \t - 0 = ERROR \n \t - 1 = WARN \n \t - 2 = DEBUG \n \t - 3 = INFO \n \t - 4 = FUNCTION \n \t - 5 = LOGIC \n \t - 6 = ALL", arg3);
    cmd.AddValue("cc", "Tell application which congestion control algorithm to use:\n \t - 0 = Uncoupled_TCPs \n \t - 1 = Linked_Increases \n \t - 2 = RTT_Compensator \n \t - 3 = Fully_Coupled", arg1);
    cmd.AddValue("pr", "Tell application which packet reordering algorithm to use:\n \t - 0 = NoPR_Algo \n \t - 1 = Eifel \n \t - 2 = TCP_DOOR \n \t - 3 = D_SACK \n \t - 4 = F_RTO",  arg2);
    cmd.AddValue("sf", "Tell application the number of subflows to be established between endpoints",  arg4);

    cmd.Parse (argc, argv);

    cc = (arg1==-1 ? Fully_Coupled:(CongestionCtrl_t) arg1);
    pr = (arg2==-1 ? D_SACK:(PacketReorder_t) arg2);
    sf = (arg4 = -1 ? 2: arg4);

    LogComponentEnable("FirstMultipathToplogy", LOG_LEVEL_ALL);
    //LogComponentEnable("TcpSocketFactory", LOG_LEVEL_ALL);
    //LogComponentEnable("ApplicationContainer", LOG_LEVEL_INFO);
    //LogComponentEnable("MpTcpL4Protocol", LOG_LEVEL_ALL);
    //LogComponentEnable("TcpL4Protocol", LOG_LEVEL_INFO);
    //LogComponentEnable("Packet", LOG_LEVEL_ALL);
    //LogComponentEnable("Socket", LOG_LEVEL_ALL);
    if(arg3 == 2)
        LogComponentEnable("MpTcpSocketImpl", LOG_DEBUG);
    else if(arg3 == 6)
        LogComponentEnable("MpTcpSocketImpl", LOG_LEVEL_ALL);
    else
        LogComponentEnable("MpTcpSocketImpl", LOG_WARN);
    //LogComponentEnable("TcpSocketImpl", LOG_LEVEL_ALL);
    LogComponentEnable("MpTcpPacketSink", LOG_WARN);
    LogComponentEnable("MpTcpHeader", LOG_WARN);
    //LogComponentEnable("TcpHeader", LOG_LEVEL_ALL);
    //LogComponentEnable("Ipv4L3Protocol", LOG_LEVEL_ALL);
    //LogComponentEnable("MpTcpTypeDefs", LOG_ERROR);
    //LogComponentEnable("RttEstimator", LOG_LEVEL_ALL);

    // Creation of the hosts
    NodeContainer nodes;
    nodes.Create(2);
    client = nodes.Get(0);
    server = nodes.Get(1);


    MpInternetStackHelper stack;
    stack.Install(nodes);

    vector<Ipv4InterfaceContainer> ipv4Ints;
    for(int i=0; i < sf; i++)
    {
        // Creation of the point to point link between hots
        PointToPointHelper p2plink;
        p2plink.SetDeviceAttribute ("DataRate", StringValue("5Mbps"));
        p2plink.SetChannelAttribute("Delay", StringValue("100ms"));

        NetDeviceContainer netDevices;
        netDevices = p2plink.Install(nodes);

        // Attribution of the IP addresses
        std::stringstream netAddr;
        netAddr << "10.1." << (i+1) << ".0";
        string str = netAddr.str();

        Ipv4AddressHelper ipv4addr;
        ipv4addr.SetBase(str.c_str(), "255.255.255.0");
        Ipv4InterfaceContainer interface = ipv4addr.Assign(netDevices);
        ipv4Ints.insert(ipv4Ints.end(), interface);
    }
/*
    fstP2Plink.SetDeviceAttribute ("DataRate", StringValue("5Mbps"));
    fstP2Plink.SetChannelAttribute("Delay", StringValue("100ms"));
    NetDeviceContainer fstDevices;
    fstDevices = fstP2Plink.Install(nodes);
*/
    // Installation of the IPv4 stack on each host
    //InternetStackHelper stack;
    //stack.Install(nodes);

    //Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Creation of the 2nd point to point link between hosts
/*
    sndP2Plink.SetDeviceAttribute ("DataRate", StringValue("5Mbps"));
    sndP2Plink.SetChannelAttribute("Delay", StringValue("100ms"));
    // Data rate and channel's delay are let in default values
    NetDeviceContainer sndDevices;
    sndDevices = sndP2Plink.Install(nodes);
*/
    // Creation of the 3rd point to point link between hots
/*
    trdP2Plink.SetDeviceAttribute ("DataRate", StringValue("3Mbps"));
    trdP2Plink.SetChannelAttribute("Delay", StringValue("10ms"));
    NetDeviceContainer trdDevices;
    trdDevices = trdP2Plink.Install(nodes);
*/
    // Enabling PCAP traces to be loged
    PointToPointHelper::EnablePcapAll("mptcp");

/*
    // Attribution of the IP addresses
    Ipv4AddressHelper fstAddrs;
    fstAddrs.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer fstInt = fstAddrs.Assign(fstDevices);

    Ipv4AddressHelper sndAddrs;
    sndAddrs.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer sndInt = sndAddrs.Assign(sndDevices);
*/
/*
    Ipv4AddressHelper trdAddrs;
    trdAddrs.SetBase("10.1.3.0", "255.255.255.0");
    Ipv4InterfaceContainer trdInt = trdAddrs.Assign(trdDevices);
*/
    // Configuration of the Client/Server application
    uint32_t servPort = 5000;
NS_LOG_INFO ("address " << ipv4Ints[0].GetAddress (1));
    ObjectFactory m_sf;
    m_sf.SetTypeId("ns3::MpTcpPacketSink");
    m_sf.Set("Protocol", StringValue ("ns3::TcpSocketFactory"));
    m_sf.Set("Local", AddressValue(InetSocketAddress (ipv4Ints[0].GetAddress (1), servPort)));
    m_sf.Set("algopr", UintegerValue ((uint32_t) pr));
    Ptr<Application> sapp = m_sf.Create<Application> ();
    server->AddApplication(sapp);
/*
    Ptr<MpTcpPacketSink> mptcpPktSnk = (Ptr<MpTcpPacketSink>) sapp;
    Ptr<MpTcpSocketImpl> sSocket = mptcpPktSnk->getMpTcpSocket ();
    sSocket->SetPacketReorderAlgo (pr);
*/
    ApplicationContainer Apps;
    Apps.Add(sapp);
//    Apps.Add(capp);


    //ApplicationContainer serverApps = sink.Install(server);
    Apps.Start(Seconds(0.0));
    Apps.Stop(Seconds(simDuration));


    //Ptr<Socket> localSocket = Socket::CreateSocket(client, TcpSocketFactory::GetTypeId());
    lSocket = new MpTcpSocketImpl (client);
    /*
    localSocket->SetNode(client);
    Ptr<MpTcpL4Protocol> mptcp = client->GetObject<MpTcpL4Protocol> ();
    localSocket->SetMpTcp(mptcp);
    */
    //lSocket->SetCongestionCtrlAlgo (Linked_Increases);
    //lSocket->SetCongestionCtrlAlgo (RTT_Compensator);
    lSocket->SetCongestionCtrlAlgo (cc);
    //lSocket->SetCongestionCtrlAlgo (Uncoupled_TCPs);

    lSocket->SetDataDistribAlgo (Round_Robin);

    //lSocket->SetPacketReorderAlgo (Eifel);
    lSocket->SetPacketReorderAlgo (pr);

    lSocket->Bind ();


    // Trace changes to the congestion window
    Config::ConnectWithoutContext ("/NodeList/0/$ns3::MpTcpSocketImpl/subflows/0/CongestionWindow", MakeCallback (&CwndTracer));

    // ...and schedule the sending "Application"; This is similar to what an
    // ns3::Application subclass would do internally.
    Simulator::ScheduleNow (&StartFlow, lSocket, ipv4Ints[0].GetAddress (1), servPort);

    // Finally, set up the simulator to run.  The 1000 second hard limit is a
    // failsafe in case some change above causes the simulation to never end
    Simulator::Stop (Seconds(simDuration + 1000.0));
    Simulator::Run ();
    Simulator::Destroy();
NS_LOG_LOGIC("mpTopology:: simulation ended");
    return 0;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//begin implementation of sending "Application"
void StartFlow(Ptr<MpTcpSocketImpl> localSocket, Ipv4Address servAddress, uint16_t servPort)
{
  NS_LOG_LOGIC("Starting flow at time " <<  Simulator::Now ().GetSeconds ());
  //localSocket->Connect (InetSocketAddress (servAddress, servPort));//connect
  lSocket->SetMaxSubFlowNumber(5);
  lSocket->SetMinSubFlowNumber(3);
  lSocket->SetSourceAddress(Ipv4Address("10.1.1.1"));
  lSocket->allocateSendingBuffer(sendBufSize);
  lSocket->allocateRecvingBuffer(recvBufSize);
  // the following buffer is uesed by the received to hold out of sequence data
  lSocket->SetunOrdBufMaxSize(50);

  int connectionState = lSocket->Connect( servAddress, servPort);
//NS_LOG_LOGIC("mpTopology:: connection request sent");
  // tell the tcp implementation to call WriteUntilBufferFull again
  // if we blocked and new tx buffer space becomes available
  if(connectionState == 0)
  {
      lSocket->SetConnectCallback  (MakeCallback (&connectionSucceeded), MakeCallback (&connectionFailed));
      lSocket->SetDataSentCallback (MakeCallback (&WriteUntilBufferFull));
      lSocket->SetCloseCallbacks   (MakeCallback (&HandlePeerClose), MakeCallback(&HandlePeerError));
      lSocket->GetSubflow(0)->StartTracing ("CongestionWindow");
  }else
  {
      //localSocket->NotifyConnectionFailed();
      NS_LOG_LOGIC("mpTopology:: connection failed");
  }
  //WriteUntilBufferFull (localSocket);
}

void connectionSucceeded (Ptr<Socket> localSocket)
{
    //NS_LOG_FUNCTION_NOARGS();
    NS_LOG_INFO("mpTopology: Connection requeste succeed");
    Simulator::Schedule (Seconds (1.0), &WriteUntilBufferFull, lSocket, 0);
    Simulator::Schedule (Seconds (simDuration), &CloseConnection, lSocket);
    //Ptr<MpTcpSocketImpl> lSock = localSocket;
    // advertise local addresses
    //lSocket->InitiateSubflows();
    //WriteUntilBufferFull(lSocket, 0);
}

void connectionFailed (Ptr<Socket> localSocket)
{
    //NS_LOG_FUNCTION_NOARGS();
    NS_LOG_INFO("mpTopology: Connection requeste failure");
    lSocket->Close();
}

void HandlePeerClose (Ptr<Socket> localSocket)
{
    //NS_LOG_FUNCTION_NOARGS();
    NS_LOG_INFO("mpTopology: Connection closed by peer");
    lSocket->Close();
}

void HandlePeerError (Ptr<Socket> localSocket)
{
    //NS_LOG_FUNCTION_NOARGS();
    NS_LOG_INFO("mpTopology: Connection closed by peer error");
    lSocket->Close();
}

void CloseConnection (Ptr<Socket> localSocket)
{
    lSocket->Close();
    NS_LOG_LOGIC("mpTopology:: currentTxBytes = " << currentTxBytes);
    NS_LOG_LOGIC("mpTopology:: totalTxBytes   = " << totalTxBytes);
    NS_LOG_LOGIC("mpTopology:: connection to remote host has been closed");
}

/*
void variateDelay(PointToPointHelper P2Plink)
{
    //NS_LOG_INFO ("variateDelay -> old delay == " << P2Plink.GetDelay());
    NS_LOG_INFO ("variateDelay");
    std::stringstream strDelay;
    int intDelay = rand() % 100;
    strDelay << intDelay << "ms";
    P2Plink.SetChannelAttribute("Delay", StringValue(strDelay.str()));

    NS_LOG_INFO ("New delay == " << strDelay.str());
}
*/

void variateDelay (Ptr<Node> node)
{
    //NS_LOG_INFO ("variateDelay");

    Ptr<Ipv4L3Protocol> ipv4 = node->GetObject<Ipv4L3Protocol> ();
    TimeValue delay;
    for(uint32_t i = 0; i < ipv4->GetNInterfaces(); i++)
    {
        //Ptr<NetDevice> device = m_node->GetDevice(i);
        Ptr<Ipv4Interface> interface = ipv4->GetInterface(i);
        Ipv4InterfaceAddress interfaceAddr = interface->GetAddress (0);
        // do not consider loopback addresses
        if(interfaceAddr.GetLocal() == Ipv4Address::GetLoopback())
        {
            // loopback interface has identifier equal to zero
            continue;
        }

        Ptr<NetDevice> netDev =  interface->GetDevice();
        Ptr<Channel> P2Plink  =  netDev->GetChannel();
        P2Plink->GetAttribute(string("Delay"), delay);
        double oldDelay = delay.Get().GetSeconds();
        //NS_LOG_INFO ("variateDelay -> old delay == " << oldDelay);
        std::stringstream strDelay;
        double newDelay = (rand() % 100) * 0.001;
        double err = newDelay - oldDelay;
        strDelay << (0.95 * oldDelay + 0.05 * err) << "s";
        P2Plink->SetAttribute(string("Delay"), StringValue(strDelay.str()));
        P2Plink->GetAttribute(string("Delay"), delay);
        //NS_LOG_INFO ("variateDelay -> new delay == " << delay.Get().GetSeconds());
    }
}

void WriteUntilBufferFull (Ptr<Socket> localSocket, unsigned int txSpace)
{
    //NS_LOG_FUNCTION_NOARGS();

    //uint32_t txSpace = localSocket->GetTxAvailable ();
  while (currentTxBytes < totalTxBytes && lSocket->GetTxAvailable () > 0)
  {
      uint32_t left    = totalTxBytes - currentTxBytes;
      uint32_t toWrite = std::min(writeSize, lSocket->GetTxAvailable ());
      toWrite = std::min( toWrite , left );
//NS_LOG_LOGIC("mpTopology:: data already sent ("<< currentTxBytes <<") data buffered ("<< toWrite << ") to be sent subsequentlly");
      int amountBuffered = lSocket->FillBuffer (&data[currentTxBytes], toWrite);
      currentTxBytes += amountBuffered;

      variateDelay(client);
/*
      variateDelay(sndP2Plink);

      variateDelay(trdP2Plink);
*/
      lSocket->SendBufferedData();
  }

/*
  if ( schedule == true )
  {
      // we will be called again when new tx space becomes available.
      NS_LOG_FUNCTION ("we have to wait a while before trying to send new data");
      //Simulator::Schedule (Seconds (2.0), &MpTcpSocketImpl::SendBufferedData, lSocket);
      Timer timer = Timer ( Timer::CANCEL_ON_DESTROY );
      timer.SetFunction ( &WriteUntilBufferFull );
      timer.Schedule ( Seconds (2.01) );
  }
*/

  //lSocket->SendBufferedData ();
  //NS_LOG_LOGIC("mpTopology::WriteUntilBufferFull leaving !");

}

