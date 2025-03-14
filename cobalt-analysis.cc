//NS3 MOUDS
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/traffic-control-module.h"
#include <iostream>
#include <map>
#include <string>

using namespace ns3;

// This structure holds data related to each traffic scenario:
struct TrafficData {
  std::map<float, uint32_t> queueSize;
  std::map<float, double>   queueDelay;
  std::map<float, double>   throughput;
  std::map<float, uint32_t> drops;
  std::map<float, uint32_t> txPackets;
  uint64_t lastDrops = 0;
  uint64_t lastTxPackets = 0;
};

// These global variables will store the statistics for our three tests
TrafficData lowCongestion;
TrafficData extremeCongestion;
TrafficData burstyTraffic;

//records the current queue size in PACKETS
// I set the scheduling to repeat every 500ms to gather enough data
void TrackQueueSize (Ptr<QueueDisc> queue, std::map<float, uint32_t>& sizeMap) {
  float time = Simulator::Now().GetSeconds();
  sizeMap[time] = queue->GetCurrentSize().GetValue();
  Simulator::Schedule(MilliSeconds(500), &TrackQueueSize, queue, std::ref(sizeMap));
}

// This function attempts to guess the queue delay based on queue size and link speed.
// It's not perfect, but it gives me some sense of how delayed packets might be.
void TrackQueueDelay (Ptr<QueueDisc> queue, std::map<float, double>& delayMap) {
  float time = Simulator::Now().GetSeconds();
  double size = static_cast<double>(queue->GetCurrentSize().GetValue());
  // The formula is "bits in queue / link bandwidth" => time in seconds => then convert to ms
  double delay = size * 8.0 / 500000.0 * 1000.0; 
  delayMap[time] = delay;
  Simulator::Schedule(MilliSeconds(500), &TrackQueueDelay, queue, std::ref(delayMap));
}

// This function calculates throughput by checking how many bytes have been received 
// by the flow monitor, then converting it into Mbps.
void TrackThroughput (Ptr<FlowMonitor> monitor, Ptr<Ipv4FlowClassifier> classifier,
                      std::map<float, double>& throughputMap) {
  monitor->CheckForLostPackets();
  auto stats = monitor->GetFlowStats();
  float time = Simulator::Now().GetSeconds();
  double totalThroughput = 0.0;

  for (auto const &flow : stats) {
    Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flow.first);
    if (t.destinationPort == 5001) {
      // bits / (time in seconds * 1e6) => Mbps
      totalThroughput += flow.second.rxBytes * 8.0 / (time * 1e6);
    }
  }
  throughputMap[time] = totalThroughput;
  Simulator::Schedule(MilliSeconds(500), &TrackThroughput, monitor, classifier, 
                      std::ref(throughputMap));
}

//Here we will track how many packets have been dropped from the queue 
//then it compares the new number of drops to the old number so we can see the change over time
void TrackDrops (Ptr<QueueDisc> queue, std::map<float, uint32_t>& dropMap,
                 uint64_t &lastDrops) {
  float time = Simulator::Now().GetSeconds();
  uint64_t currentDrops = queue->GetStats().nTotalDroppedPackets;
  dropMap[time] = currentDrops - lastDrops;
  lastDrops = currentDrops;
  Simulator::Schedule(MilliSeconds(500), &TrackDrops, queue, std::ref(dropMap),
                      std::ref(lastDrops));
}

// This function counts how many packets have been transmitted overall.
void TrackTxPackets (Ptr<FlowMonitor> monitor, Ptr<Ipv4FlowClassifier> classifier,
                     std::map<float, uint32_t>& txPacketsMap, uint64_t &lastTxPackets) {
  monitor->CheckForLostPackets();
  auto stats = monitor->GetFlowStats();
  float time = Simulator::Now().GetSeconds();

  uint64_t totalTxPackets = 0;
  for (auto const &flow : stats) {
    totalTxPackets += flow.second.txPackets;
  }
  txPacketsMap[time] = totalTxPackets - lastTxPackets;
  lastTxPackets = totalTxPackets;
  Simulator::Schedule(MilliSeconds(500), &TrackTxPackets, monitor, classifier, 
                      std::ref(txPacketsMap), std::ref(lastTxPackets));
}
// This function sets up the topology, installs the COBALT queue, runs the traffic sources,
// and collects final statistics for a single scenario.
void RunTest (std::string dataRate, std::string testType, double simTime) 
{
  // We completely clear any existing NS-3 simulation state
  Simulator::Destroy();

  // Create our four nodes: n0, r1, r2, n3
  NodeContainer nodes;
  nodes.Create(4);

  // Setup the bottleneck link (500kbps) and the edge links (10Mbps).
  PointToPointHelper bottleneck;
  bottleneck.SetDeviceAttribute("DataRate", StringValue("500kbps"));
  bottleneck.SetChannelAttribute("Delay", StringValue("20ms"));

  PointToPointHelper edge;
  edge.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
  edge.SetChannelAttribute("Delay", StringValue("1ms"));

  //Install the network devices on the nodes for each link.
  NetDeviceContainer d01 = edge.Install(nodes.Get(0), nodes.Get(1));
  NetDeviceContainer d12 = bottleneck.Install(nodes.Get(1), nodes.Get(2));
  NetDeviceContainer d23 = edge.Install(nodes.Get(2), nodes.Get(3));

  //The InternetStackHelper is needed so we can use TCP/IP on these nodes.
  InternetStackHelper internet;
  internet.Install(nodes);

  //Set up COBALT QUE system to manage congestion.
  TrafficControlHelper tchCobalt;
  tchCobalt.SetRootQueueDisc("ns3::CobaltQueueDisc",
                             "Target",   TimeValue(MilliSeconds(5)),
                             "Interval", TimeValue(MilliSeconds(100)),
                             "MaxSize",  QueueSizeValue(QueueSize(QueueSizeUnit::PACKETS, 100)));
  QueueDiscContainer qdisc = tchCobalt.Install(d12.Get(0));

  //Assign IP addresses to the NetDevices soi that nodes can communicate
  Ipv4AddressHelper ipv4;
  ipv4.SetBase("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer i01 = ipv4.Assign(d01);
  ipv4.SetBase("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer i12 = ipv4.Assign(d12);
  ipv4.SetBase("10.1.3.0", "255.255.255.0");
  Ipv4InterfaceContainer i23 = ipv4.Assign(d23);

  //PopulateRoutingTables so NS-3 can automatically configures routes
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  //Create a PacketSink application to receive data on node n3.
  uint16_t port = 5001;
  PacketSinkHelper sink("ns3::TcpSocketFactory",
                        InetSocketAddress(Ipv4Address::GetAny(), port));
  ApplicationContainer sinkApp = sink.Install(nodes.Get(3));
  sinkApp.Start(Seconds(0.0));
  sinkApp.Stop(Seconds(simTime));

  //Create a TCP OnOff traffic source on node n0.
  OnOffHelper source("ns3::TcpSocketFactory", 
                     InetSocketAddress(i23.GetAddress(1), port));
  if (testType == "Bursty") {
    //For Bursty traffic, we set short on-times and longer off-times.
    source.SetAttribute("DataRate",   StringValue("100Mbps"));
    source.SetAttribute("PacketSize", UintegerValue(1460));
    source.SetAttribute("OnTime",     StringValue("ns3::ConstantRandomVariable[Constant=0.1]"));
    source.SetAttribute("OffTime",    StringValue("ns3::ConstantRandomVariable[Constant=0.5]"));
  } else {
    //For Low or Extreme, we use a fixed rate and keep the source on 100% of the time.
    source.SetAttribute("DataRate",   StringValue(dataRate));
    source.SetAttribute("PacketSize", UintegerValue(1460));
    source.SetAttribute("OnTime",     StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    source.SetAttribute("OffTime",    StringValue("ns3::ConstantRandomVariable[Constant=0]"));
  }
  ApplicationContainer sourceApp = source.Install(nodes.Get(0));
  sourceApp.Start(Seconds(1.0));
  sourceApp.Stop(Seconds(simTime - 0.5));

  //If we're doing the Bursty scenario, we add a second source for extra bursts.
  if (testType == "Bursty") {
    OnOffHelper source2("ns3::TcpSocketFactory", 
                        InetSocketAddress(i23.GetAddress(1), port));
    source2.SetAttribute("DataRate",   StringValue("80Mbps"));
    source2.SetAttribute("PacketSize", UintegerValue(1460));
    source2.SetAttribute("OnTime",     StringValue("ns3::ConstantRandomVariable[Constant=0.2]"));
    source2.SetAttribute("OffTime",    StringValue("ns3::ConstantRandomVariable[Constant=0.3]"));

    ApplicationContainer sourceApp2 = source2.Install(nodes.Get(0));
    sourceApp2.Start(Seconds(1.5));
    sourceApp2.Stop(Seconds(simTime - 0.5));
  }

  //The FlowMonitor tracks data flows reports back the sats we are looking for.
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());

  //decides what global TrafficData object to use based on the the current test sceneario
  TrafficData *data = nullptr;
  if (testType == "Extreme") data = &extremeCongestion;
  else if (testType == "Bursty") data = &burstyTraffic;
  else data = &lowCongestion;

  // Schedule periodic calls to our tracking functions.
  Simulator::Schedule(Seconds(0.0), &TrackQueueSize,    qdisc.Get(0), std::ref(data->queueSize));
  Simulator::Schedule(Seconds(0.0), &TrackQueueDelay,   qdisc.Get(0), std::ref(data->queueDelay));
  Simulator::Schedule(Seconds(0.5), &TrackThroughput,   monitor, classifier, std::ref(data->throughput));
  Simulator::Schedule(Seconds(0.0), &TrackDrops,        qdisc.Get(0), std::ref(data->drops),
                      std::ref(data->lastDrops));
  Simulator::Schedule(Seconds(0.0), &TrackTxPackets,    monitor, classifier, std::ref(data->txPackets),
                      std::ref(data->lastTxPackets));

  //Run the Sim 
  Simulator::Stop(Seconds(simTime));
  Simulator::Run();

  // After running, we gather statistics from the FlowMonitor
  monitor->CheckForLostPackets();
  auto stats = monitor->GetFlowStats();

  std::cout << "==================================================\n"
            << "  COBALT Traffic Type: " << testType << "\n"
            << "==================================================\n";

  double totalDelay = 0.0, totalJitter = 0.0;
  uint64_t totalTxPackets = 0, totalRxPackets = 0, totalLostPackets = 0;
  uint32_t flowCount = 0;

  //loop over each flow that the FlowMonitor recorded and sum up everything for the final summary
  for (auto const &flow : stats) {
    Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flow.first);

    std::cout << "Flow " << flow.first << " (" << t.sourceAddress
              << " -> " << t.destinationAddress << ")\n"
              << "  Tx Packets: "   << flow.second.txPackets << "\n"
              << "  Rx Packets: "   << flow.second.rxPackets << "\n"
              << "  Lost Packets: " << flow.second.lostPackets << "\n";

    totalTxPackets   += flow.second.txPackets;
    totalRxPackets   += flow.second.rxPackets;
    totalLostPackets += flow.second.lostPackets;

    if (flow.second.txPackets > 0) {
      double dropRate = 100.0 * flow.second.lostPackets / flow.second.txPackets;
      std::cout << "  Drop Rate: " << dropRate << "%\n";
    }
    if (flow.second.rxPackets > 0) {
      double meanDelay  = flow.second.delaySum.GetSeconds()  * 1000.0 / flow.second.rxPackets;
      double meanJitter = flow.second.jitterSum.GetSeconds() * 1000.0 / flow.second.rxPackets;
      std::cout << "  Mean Delay: "  << meanDelay  << " ms\n"
                << "  Mean Jitter: " << meanJitter << " ms\n";
      totalDelay  += meanDelay;
      totalJitter += meanJitter;
      flowCount++;
    }
    std::cout << std::endl;
  }

  //Final summary for tests
  std::cout << "Summary Statistics:\n"
            << "  Total Tx Packets: "   << totalTxPackets   << "\n"
            << "  Total Rx Packets: "   << totalRxPackets   << "\n"
            << "  Total Lost Packets: " << totalLostPackets << "\n";
  if (totalTxPackets > 0) {
    double overallDrop = 100.0 * totalLostPackets / totalTxPackets;
    std::cout << "  Overall Drop Rate: " << overallDrop << "%\n";
  }
  if (flowCount > 0) {
    std::cout << "  Average Delay: "  << (totalDelay  / flowCount) << " ms\n"
              << "  Average Jitter: " << (totalJitter / flowCount) << " ms\n";
  }
  std::cout << std::endl;

  //destroy current state
  Simulator::Destroy();
}

int main (int argc, char *argv[]) {
  // Set to 60s of simulation time
  double simTime = 60.0;
  CommandLine cmd;
  cmd.AddValue("simTime", "Simulation time in seconds", simTime);
  cmd.Parse(argc, argv);

  //Our three tests "Low", "Extreme", and "Bursty"
  RunTest("1Mbps",  "Low",     simTime);
  RunTest("6Mbps",  "Extreme", simTime);
  RunTest("Bursty", "Bursty",  simTime);

  std::cout << "All tests completed.\n";
  return 0;
}



