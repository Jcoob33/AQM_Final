#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/traffic-control-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("SimpleRedExample");

int main(int argc, char *argv[])
{
  // Simulation parameters
  double simTime = 10.0;  // seconds
  uint16_t port = 5001;
  
  // RED parameters
  double minTh = 5;
  double maxTh = 15;
  uint32_t queueSize = 100;  // reduced for simplicity
  
  // Enable logging
  LogComponentEnable("SimpleRedExample", LOG_LEVEL_INFO);
  
  // Create a simple topology: n0 -- r1 -- r2 -- n3
  // Where r1-r2 is the bottleneck link with RED
  NodeContainer nodes;
  nodes.Create(4);
  
  // Name nodes for clarity
  Names::Add("Source", nodes.Get(0));
  Names::Add("Router1", nodes.Get(1));
  Names::Add("Router2", nodes.Get(2));
  Names::Add("Sink", nodes.Get(3));
  
  // Create the bottleneck link
  PointToPointHelper bottleneck;
  bottleneck.SetDeviceAttribute("DataRate", StringValue("1Mbps"));
  bottleneck.SetChannelAttribute("Delay", StringValue("20ms"));
  
  // Create the edge links
  PointToPointHelper edge;
  edge.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
  edge.SetChannelAttribute("Delay", StringValue("1ms"));
  
  // Install devices
  NetDeviceContainer d01 = edge.Install(nodes.Get(0), nodes.Get(1));
  NetDeviceContainer d12 = bottleneck.Install(nodes.Get(1), nodes.Get(2));
  NetDeviceContainer d23 = edge.Install(nodes.Get(2), nodes.Get(3));
  
  // Install the internet stack
  InternetStackHelper internet;
  internet.Install(nodes);
  
  // Setup RED queue disc on the bottleneck interface
  TrafficControlHelper tchRed;
  tchRed.SetRootQueueDisc("ns3::RedQueueDisc",
                          "MinTh", DoubleValue(minTh),
                          "MaxTh", DoubleValue(maxTh),
                          "LinkBandwidth", StringValue("1Mbps"),
                          "LinkDelay", StringValue("20ms"),
                          "MeanPktSize", UintegerValue(512),
                          "MaxSize", QueueSizeValue(QueueSize(QueueSizeUnit::PACKETS, queueSize)));
  
  // Install RED on router1's outgoing interface
  tchRed.Install(d12.Get(0));
  
  // Assign IP addresses
  Ipv4AddressHelper ipv4;
  ipv4.SetBase("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer i01 = ipv4.Assign(d01);
  
  ipv4.SetBase("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer i12 = ipv4.Assign(d12);
  
  ipv4.SetBase("10.1.3.0", "255.255.255.0");
  Ipv4InterfaceContainer i23 = ipv4.Assign(d23);
  
  // Set up the routing
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();
  
  // Create a packet sink on the sink node
  PacketSinkHelper sink("ns3::TcpSocketFactory", 
                        InetSocketAddress(Ipv4Address::GetAny(), port));
  ApplicationContainer sinkApp = sink.Install(nodes.Get(3));
  sinkApp.Start(Seconds(0.0));
  sinkApp.Stop(Seconds(simTime));
  
  // Create a TCP traffic source
  OnOffHelper source("ns3::TcpSocketFactory", 
                     InetSocketAddress(i23.GetAddress(1), port));
  source.SetAttribute("DataRate", StringValue("5Mbps")); // Higher than bottleneck to create congestion
  source.SetAttribute("PacketSize", UintegerValue(512));
  source.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
  source.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
  
  ApplicationContainer sourceApp = source.Install(nodes.Get(0));
  sourceApp.Start(Seconds(1.0));
  sourceApp.Stop(Seconds(simTime - 0.5));
  
  // Setup flow monitor
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll();
  
  NS_LOG_INFO("Running simulation with RED for " << simTime << " seconds");
  Simulator::Stop(Seconds(simTime));
  Simulator::Run();
  
  // Print statistics
  monitor->CheckForLostPackets();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();
  
  std::cout << "RED Queue Statistics:" << std::endl;
  
  for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin(); i != stats.end(); ++i) {
    Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i->first);
    
    std::cout << "Flow " << i->first << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")" << std::endl;
    std::cout << "  Tx Packets: " << i->second.txPackets << std::endl;
    std::cout << "  Rx Packets: " << i->second.rxPackets << std::endl;
    std::cout << "  Lost Packets: " << i->second.lostPackets << std::endl;
    
    if (i->second.txPackets > 0) {
      std::cout << "  Drop Rate: " << i->second.lostPackets * 100.0 / i->second.txPackets << "%" << std::endl;
    }
    
    if (i->second.rxPackets > 0) {
      std::cout << "  Mean Delay: " << i->second.delaySum.GetSeconds() * 1000 / i->second.rxPackets << " ms" << std::endl;
      std::cout << "  Mean Jitter: " << i->second.jitterSum.GetSeconds() * 1000 / i->second.rxPackets << " ms" << std::endl;
    }
    std::cout << std::endl;
  }
  
  Simulator::Destroy();
  return 0;
}