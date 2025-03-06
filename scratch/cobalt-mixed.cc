#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/traffic-control-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("CobaltMixedTraffic");

int main(int argc, char *argv[])
{
  double simTime = 10.0;
  uint16_t udpPort = 4000;
  uint16_t tcpPort = 5000;

  LogComponentEnable("CobaltMixedTraffic", LOG_LEVEL_INFO);

  NodeContainer nodes;
  nodes.Create(4);

  PointToPointHelper bottleneck;
  bottleneck.SetDeviceAttribute("DataRate", StringValue("500Kbps"));
  bottleneck.SetChannelAttribute("Delay", StringValue("20ms"));

  PointToPointHelper edge;
  edge.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
  edge.SetChannelAttribute("Delay", StringValue("1ms"));

  NetDeviceContainer d01 = edge.Install(nodes.Get(0), nodes.Get(1));
  NetDeviceContainer d12 = bottleneck.Install(nodes.Get(1), nodes.Get(2));
  NetDeviceContainer d23 = edge.Install(nodes.Get(2), nodes.Get(3));

  InternetStackHelper internet;
  internet.Install(nodes);

  TrafficControlHelper tchCobalt;
  tchCobalt.SetRootQueueDisc("ns3::CobaltQueueDisc", "Target", TimeValue(MilliSeconds(2)), "Interval", TimeValue(MilliSeconds(50)), "Increment", DoubleValue(0.01));
  tchCobalt.Install(d12.Get(0));

  Ipv4AddressHelper ipv4;
  ipv4.SetBase("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer i01 = ipv4.Assign(d01);

  ipv4.SetBase("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer i12 = ipv4.Assign(d12);

  ipv4.SetBase("10.1.3.0", "255.255.255.0");
  Ipv4InterfaceContainer i23 = ipv4.Assign(d23);

  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  // UDP Traffic
  OnOffHelper udpSource("ns3::UdpSocketFactory", InetSocketAddress(i23.GetAddress(1), udpPort));
  udpSource.SetAttribute("DataRate", StringValue("3Mbps"));
  udpSource.SetAttribute("PacketSize", UintegerValue(1024));
  udpSource.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
  udpSource.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
  ApplicationContainer udpApp = udpSource.Install(nodes.Get(0));
  udpApp.Start(Seconds(1.0));
  udpApp.Stop(Seconds(simTime - 1.0));

  PacketSinkHelper udpSink("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), udpPort));
  ApplicationContainer udpSinkApp = udpSink.Install(nodes.Get(3));
  udpSinkApp.Start(Seconds(0.0));
  udpSinkApp.Stop(Seconds(simTime));

  // TCP Traffic
  BulkSendHelper tcpSource("ns3::TcpSocketFactory", InetSocketAddress(i23.GetAddress(1), tcpPort));
  tcpSource.SetAttribute("MaxBytes", UintegerValue(0)); // Unlimited Data
  ApplicationContainer tcpApp = tcpSource.Install(nodes.Get(0));
  tcpApp.Start(Seconds(1.0));
  tcpApp.Stop(Seconds(simTime - 1.0));

  PacketSinkHelper tcpSink("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), tcpPort));
  ApplicationContainer tcpSinkApp = tcpSink.Install(nodes.Get(3));
  tcpSinkApp.Start(Seconds(0.0));
  tcpSinkApp.Stop(Seconds(simTime));

  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll();

  NS_LOG_INFO("Running CoBALT with Mixed Traffic for " << simTime << " seconds");
  Simulator::Stop(Seconds(simTime));
  Simulator::Run();

  monitor->CheckForLostPackets();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

  std::cout << "CoBALT Mixed Traffic Statistics:" << std::endl;

  for (const auto &i : stats)
  {
    Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i.first);

    std::cout << "Flow " << i.first << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")" << std::endl;
    std::cout << "  Tx Packets: " << i.second.txPackets << std::endl;
    std::cout << "  Rx Packets: " << i.second.rxPackets << std::endl;
    std::cout << "  Lost Packets: " << i.second.lostPackets << std::endl;

    if (i.second.txPackets > 0)
    {
      std::cout << "  Drop Rate: " << (i.second.lostPackets * 100.0 / i.second.txPackets) << "%" << std::endl;
    }

    if (i.second.rxPackets > 0)
    {
      std::cout << "  Mean Delay: " << (i.second.delaySum.GetSeconds() * 1000 / i.second.rxPackets) << " ms" << std::endl;
      std::cout << "  Mean Jitter: " << (i.second.jitterSum.GetSeconds() * 1000 / i.second.rxPackets) << " ms" << std::endl;
    }
    std::cout << std::endl;
  }

  Simulator::Destroy();
  return 0;
}
