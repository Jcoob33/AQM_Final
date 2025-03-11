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
  uint32_t numFlows = 5; // Number of TCP flows to generate
  
  // RED parameters
  double minTh = 5;
  double maxTh = 15;
  uint32_t queueSize = 100;  // queue size in packets
  
  // Enable logging
  LogComponentEnable("SimpleRedExample", LOG_LEVEL_INFO);
  
  // Create a simple topology: Multiple sources -- r1 -- r2 -- Multiple sinks
  // Where r1-r2 is the bottleneck link with RED
  NodeContainer routers;
  routers.Create(2);
  
  NodeContainer sources;
  sources.Create(numFlows);
  
  NodeContainer sinks;
  sinks.Create(numFlows);
  
  // Name nodes for clarity
  Names::Add("Router1", routers.Get(0));
  Names::Add("Router2", routers.Get(1));
  
  // Create the bottleneck link
  PointToPointHelper bottleneck;
  bottleneck.SetDeviceAttribute("DataRate", StringValue("1Mbps"));
  bottleneck.SetChannelAttribute("Delay", StringValue("20ms"));
  
  // Create the edge links
  PointToPointHelper edge;
  edge.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
  edge.SetChannelAttribute("Delay", StringValue("1ms"));
  
  // Install devices for bottleneck link
  NetDeviceContainer routerDevices = bottleneck.Install(routers);
  
  // Connect sources to router1 and sinks to router2
  NetDeviceContainer sourceDevices;
  NetDeviceContainer sinkDevices;
  
  for (uint32_t i = 0; i < numFlows; i++) {
    NetDeviceContainer srcDev = edge.Install(sources.Get(i), routers.Get(0));
    sourceDevices.Add(srcDev.Get(0));
    
    NetDeviceContainer snkDev = edge.Install(routers.Get(1), sinks.Get(i));
    sinkDevices.Add(snkDev.Get(1));
  }
  
  // Install the internet stack
  InternetStackHelper internet;
  internet.Install(routers);
  internet.Install(sources);
  internet.Install(sinks);
  
  // Setup RED queue disc on the bottleneck interface
  TrafficControlHelper tchRed;
  tchRed.SetRootQueueDisc("ns3::RedQueueDisc",
                          "MinTh", DoubleValue(minTh),
                          "MaxTh", DoubleValue(maxTh),
                          "LinkBandwidth", StringValue("1Mbps"),
                          "LinkDelay", StringValue("20ms"),
                          "MeanPktSize", UintegerValue(512),
                          "MaxSize", QueueSizeValue(QueueSize(QueueSizeUnit::PACKETS, queueSize)));
  
  // Install RED on router1's outgoing interface to router2
  tchRed.Install(routerDevices.Get(0));
  
  // Assign IP addresses
  Ipv4AddressHelper ipv4;
  
  // Bottleneck link addresses
  ipv4.SetBase("10.1.0.0", "255.255.255.0");
  Ipv4InterfaceContainer routerIfaces = ipv4.Assign(routerDevices);
  
  // Source and sink addresses
  std::vector<Ipv4InterfaceContainer> sourceIfaces(numFlows);
  std::vector<Ipv4InterfaceContainer> sinkIfaces(numFlows);
  
  for (uint32_t i = 0; i < numFlows; i++) {
    std::ostringstream subnet;
    subnet << "10." << (i + 2) << ".0.0";
    ipv4.SetBase(subnet.str().c_str(), "255.255.255.0");
    
    NetDeviceContainer srcToR1;
    srcToR1.Add(sourceDevices.Get(i));
    srcToR1.Add(routers.Get(0)->GetDevice(i + 1)); // +1 because device 0 is the bottleneck
    sourceIfaces[i] = ipv4.Assign(srcToR1);
    
    subnet.str("");
    subnet << "10." << (i + 2 + numFlows) << ".0.0";
    ipv4.SetBase(subnet.str().c_str(), "255.255.255.0");
    
    NetDeviceContainer r2ToSnk;
    r2ToSnk.Add(routers.Get(1)->GetDevice(i + 1)); // +1 because device 0 is the bottleneck
    r2ToSnk.Add(sinkDevices.Get(i));
    sinkIfaces[i] = ipv4.Assign(r2ToSnk);
  }
  
  // Set up the routing
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();
  
  // Create packet sinks on all sink nodes
  PacketSinkHelper sink("ns3::TcpSocketFactory", 
                        InetSocketAddress(Ipv4Address::GetAny(), port));
  ApplicationContainer sinkApps;
  
  for (uint32_t i = 0; i < numFlows; i++) {
    sinkApps.Add(sink.Install(sinks.Get(i)));
  }
  
  sinkApps.Start(Seconds(0.0));
  sinkApps.Stop(Seconds(simTime));
  
  // Create TCP traffic sources with variable rates to create different levels of congestion
  ApplicationContainer sourceApps;
  
  for (uint32_t i = 0; i < numFlows; i++) {
    // Calculate a data rate between 1 and 3 Mbps for each flow
    // This will create total traffic of 5-15 Mbps, well above the 1 Mbps bottleneck
    std::ostringstream rate;
    rate << (1 + (i % 3)) << "Mbps"; // Gives 1, 2, 3, 1, 2 Mbps for 5 flows
    
    // Create TCP connection from source to sink
    OnOffHelper source("ns3::TcpSocketFactory", 
                      InetSocketAddress(sinkIfaces[i].GetAddress(1), port));
    source.SetAttribute("DataRate", StringValue(rate.str()));
    source.SetAttribute("PacketSize", UintegerValue(512));
    source.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    source.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    
    // Stagger start times to create more varied traffic patterns
    ApplicationContainer app = source.Install(sources.Get(i));
    app.Start(Seconds(1.0 + 0.1 * i));
    app.Stop(Seconds(simTime - 0.5));
    
    sourceApps.Add(app);
    
    NS_LOG_INFO("Created flow " << i << " with rate " << rate.str());
  }
  
  // Setup flow monitor
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll();
  
  NS_LOG_INFO("Running simulation with RED and " << numFlows << " flows for " << simTime << " seconds");
  Simulator::Stop(Seconds(simTime));
  Simulator::Run();
  
  // Print statistics
  monitor->CheckForLostPackets();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();
  
  std::cout << "RED Queue Statistics:" << std::endl;
  
  double totalTxPackets = 0;
  double totalRxPackets = 0;
  double totalLostPackets = 0;
  double totalDelaySum = 0;
  double totalJitterSum = 0;
  
  for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin(); i != stats.end(); ++i) {
    Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i->first);
    
    // Skip non-TCP flows
    if (t.protocol != 6) continue;
    
    std::cout << "Flow " << i->first << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")" << std::endl;
    std::cout << "  Tx Packets: " << i->second.txPackets << std::endl;
    std::cout << "  Rx Packets: " << i->second.rxPackets << std::endl;
    std::cout << "  Lost Packets: " << i->second.lostPackets << std::endl;
    
    totalTxPackets += i->second.txPackets;
    totalRxPackets += i->second.rxPackets;
    totalLostPackets += i->second.lostPackets;
    totalDelaySum += i->second.delaySum.GetSeconds();
    totalJitterSum += i->second.jitterSum.GetSeconds();
    
    if (i->second.txPackets > 0) {
      std::cout << "  Drop Rate: " << i->second.lostPackets * 100.0 / i->second.txPackets << "%" << std::endl;
    }
    
    if (i->second.rxPackets > 0) {
      std::cout << "  Mean Delay: " << i->second.delaySum.GetSeconds() * 1000 / i->second.rxPackets << " ms" << std::endl;
      std::cout << "  Mean Jitter: " << i->second.jitterSum.GetSeconds() * 1000 / i->second.rxPackets << " ms" << std::endl;
    }
    std::cout << std::endl;
  }
  
  // Print aggregate statistics
  std::cout << "*** Aggregate RED Performance Metrics ***" << std::endl;
  std::cout << "Total Tx Packets: " << totalTxPackets << std::endl;
  std::cout << "Total Rx Packets: " << totalRxPackets << std::endl;
  std::cout << "Total Lost Packets: " << totalLostPackets << std::endl;
  
  if (totalTxPackets > 0) {
    std::cout << "Overall Packet Drop Rate: " << totalLostPackets * 100.0 / totalTxPackets << "%" << std::endl;
  }
  
  if (totalRxPackets > 0) {
    std::cout << "Average End-to-End Delay: " << totalDelaySum * 1000 / totalRxPackets << " ms" << std::endl;
    std::cout << "Average Jitter: " << totalJitterSum * 1000 / totalRxPackets << " ms" << std::endl;
  }
  
  Simulator::Destroy();
  return 0;
}