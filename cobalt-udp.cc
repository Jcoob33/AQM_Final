#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/gnuplot.h"
#include <fstream>
#include <string>
#include <cmath>

using namespace ns3;

/**
 * Logging component for the CobaltUdpAnalysis simulation.
 */
NS_LOG_COMPONENT_DEFINE("CobaltUdpAnalysis");

/**
 * Structure to hold traffic data (metrics) for each scenario.
 *
 * Each member is a time-series map (with float time as key), and some counters
 * for tracking packet drops/transmissions since the last measurement.
 */
struct TrafficData {
  std::map<float, uint32_t> queueSize;     // Queue size data over time (packets)
  std::map<float, double>   queueDelay;    // Approx queue delay data over time (ms)
  std::map<float, double>   throughput;    // Throughput data over time (Mbps)
  std::map<float, uint32_t> drops;         // Packet drops over time
  std::map<float, uint32_t> txPackets;     // Transmitted packets over time
  uint64_t lastDrops     = 0;              // For calculating new drops per interval
  uint64_t lastTxPackets = 0;              // For calculating new transmitted packets per interval
};

// Global TrafficData structures for our three scenarios
TrafficData lowCongestion;
TrafficData extremeCongestion;
TrafficData burstyTraffic;

/**
 * Periodically record the number of packets in the queue.
 *
 * @param queue   Pointer to the QueueDisc whose size we're tracking
 * @param sizeMap Reference to the map that stores time -> queue size
 */
void
TrackQueueSize (Ptr<QueueDisc> queue, std::map<float, uint32_t>& sizeMap)
{
  float time    = Simulator::Now().GetSeconds();
  uint32_t size = queue->GetCurrentSize().GetValue();
  sizeMap[time] = size;

  // Schedule again in 500 ms
  Simulator::Schedule(MilliSeconds(500), &TrackQueueSize, queue, std::ref(sizeMap));
}

/**
 * Periodically estimate the queue delay based on queue size and link speed.
 *
 * @param queue    Pointer to the QueueDisc whose delay we approximate
 * @param delayMap Reference to the map storing time -> queue delay in ms
 */
void
TrackQueueDelay (Ptr<QueueDisc> queue, std::map<float, double>& delayMap)
{
  float time    = Simulator::Now().GetSeconds();
  uint32_t qSize = queue->GetCurrentSize().GetValue();

  // Approx: (queueSize (packets) * 8 bits/byte) / 500000 bps => seconds => *1000 => ms
  double delay = qSize * 8.0 / 500000.0 * 1000.0;
  delayMap[time] = delay;

  // Schedule again in 500 ms
  Simulator::Schedule(MilliSeconds(500), &TrackQueueDelay, queue, std::ref(delayMap));
}

/**
 * Periodically calculate throughput from the FlowMonitor stats for UDP flows on port 4000.
 *
 * @param monitor       Pointer to the FlowMonitor object tracking flows
 * @param classifier    Classifier used to identify flows by port numbers, etc.
 * @param throughputMap Reference to the map storing time -> total throughput (Mbps)
 */
void
TrackThroughput (Ptr<FlowMonitor> monitor, Ptr<Ipv4FlowClassifier> classifier,
                 std::map<float, double>& throughputMap)
{
  monitor->CheckForLostPackets();
  auto stats = monitor->GetFlowStats();
  float time = Simulator::Now().GetSeconds();
  double totalThroughput = 0.0;

  for (auto &flow : stats)
  {
    Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flow.first);
    // Count only UDP flows destined for port 4000
    if (t.destinationPort == 4000)
    {
      double throughput = flow.second.rxBytes * 8.0 / (time * 1e6); // bits to Mbps
      totalThroughput  += throughput;
    }
  }
  throughputMap[time] = totalThroughput;
  Simulator::Schedule(MilliSeconds(500), &TrackThroughput, monitor, classifier, std::ref(throughputMap));
}

/**
 * Periodically track how many packets the queue has dropped since last check.
 *
 * @param queue     Pointer to the QueueDisc that might drop packets
 * @param dropMap   Reference to the map storing time -> drop count for this interval
 * @param lastDrops Reference to the counter storing the total drops previously seen
 */
void
TrackDrops (Ptr<QueueDisc> queue, std::map<float, uint32_t>& dropMap,
            uint64_t &lastDrops)
{
  float time          = Simulator::Now().GetSeconds();
  uint64_t currentDrops = queue->GetStats().nTotalDroppedPackets;

  uint32_t newDrops = currentDrops - lastDrops;
  lastDrops         = currentDrops;

  dropMap[time] = newDrops;
  Simulator::Schedule(MilliSeconds(500), &TrackDrops, queue, std::ref(dropMap),
                      std::ref(lastDrops));
}

/**
 * Periodically track how many packets have been transmitted by the flows since last check.
 *
 * @param monitor       FlowMonitor pointer to gather tx stats
 * @param classifier    Classifier for identifying flows
 * @param txPacketsMap  Reference to the map storing time -> transmitted packets in this interval
 * @param lastTxPackets Reference to the counter for how many had been transmitted previously
 */
void
TrackTxPackets (Ptr<FlowMonitor> monitor, Ptr<Ipv4FlowClassifier> classifier,
                std::map<float, uint32_t>& txPacketsMap, uint64_t &lastTxPackets)
{
  monitor->CheckForLostPackets();
  auto stats = monitor->GetFlowStats();
  float time = Simulator::Now().GetSeconds();

  uint64_t totalTxPackets = 0;
  for (auto &flow : stats)
  {
    totalTxPackets += flow.second.txPackets;
  }

  uint32_t newTxPackets = totalTxPackets - lastTxPackets;
  lastTxPackets = totalTxPackets;

  txPacketsMap[time] = newTxPackets;
  Simulator::Schedule(MilliSeconds(500), &TrackTxPackets, monitor, classifier,
                      std::ref(txPacketsMap), std::ref(lastTxPackets));
}

/**
 * Generate a Gnuplot chart comparing Low, Extreme, and Bursty data sets.
 *
 * @param title      The title of the plot (e.g., "COBALT UDP Queue Size Comparison")
 * @param yLabel     The label for the Y-axis (e.g., "Queue Size (bytes)")
 * @param filename   Base name for the output files (the script and PNG will have this name)
 * @param lowData    Map of time -> metric for the Low scenario
 * @param extremeData Map of time -> metric for the Extreme scenario
 * @param burstyData Map of time -> metric for the Bursty scenario
 */
void
GeneratePlot (const std::string& title,
              const std::string& yLabel,
              const std::string& filename,
              const std::map<float, double>& lowData,
              const std::map<float, double>& extremeData,
              const std::map<float, double>& burstyData)
{
  std::string graphicsFileName = "UDP-" + filename + ".png";
  std::string plotFileName     = "UDP-" + filename + ".plt";
  
  Gnuplot plot (graphicsFileName);
  plot.SetTitle(title);
  plot.SetTerminal("png");
  plot.SetLegend("Time (s)", yLabel);
  plot.AppendExtra("set grid");
  
  // Prepare datasets for Low, Extreme, and Bursty with line style only
  Gnuplot2dDataset lowDataset("Low Congestion");
  lowDataset.SetStyle(Gnuplot2dDataset::LINES);

  int count = 0;
  for (auto const& kv : lowData)
  {
    if (count % 10 == 0) { lowDataset.Add(kv.first, kv.second); }
    count++;
  }

  Gnuplot2dDataset extremeDataset("Extreme Congestion");
  extremeDataset.SetStyle(Gnuplot2dDataset::LINES);

  count = 0;
  for (auto const& kv : extremeData)
  {
    if (count % 10 == 0) { extremeDataset.Add(kv.first, kv.second); }
    count++;
  }

  Gnuplot2dDataset burstyDataset("Bursty Traffic");
  burstyDataset.SetStyle(Gnuplot2dDataset::LINES);

  count = 0;
  for (auto const& kv : burstyData)
  {
    if (count % 10 == 0) { burstyDataset.Add(kv.first, kv.second); }
    count++;
  }

  // Style lines (color, thickness, etc.)
  plot.AppendExtra("set autoscale");
  plot.AppendExtra("set key top right");
  plot.AppendExtra("set style line 1 lc rgb '#FF0000' lt 1 lw 4");     
  plot.AppendExtra("set style line 2 lc rgb '#0000FF' lt 1 lw 4");     
  plot.AppendExtra("set style line 3 lc rgb '#00AA00' lt 1 lw 4");     
  plot.AppendExtra("set terminal png size 1200,800 enhanced");

  lowDataset.SetExtra("linestyle 1");
  extremeDataset.SetExtra("linestyle 2");
  burstyDataset.SetExtra("linestyle 3");

  plot.AddDataset(lowDataset);
  plot.AddDataset(extremeDataset);
  plot.AddDataset(burstyDataset);
  
  // Output the final plot and script
  std::ofstream plotFile (plotFileName.c_str());
  plot.GenerateOutput(plotFile);
  plotFile.close();
}

/**
 * Sets up a UDP simulation scenario (Low, Extreme, Bursty), runs it, and prints FlowMonitor results.
 *
 * @param dataRate The data rate string for the OnOff application (e.g. "1Mbps", "100Mbps")
 * @param testType The scenario type ("Low", "Extreme", or "Bursty") which determines traffic patterns
 * @param simTime  Total simulation time in seconds
 */
void
RunTest (std::string dataRate, std::string testType, double simTime)
{
  NS_LOG_INFO("Running COBALT UDP test with " << dataRate
               << " data rate for " << testType << " congestion");
  
  // Clear simulator state in case anything persists from previous runs
  Simulator::Destroy();
  Names::Clear();
  
  // Four nodes: n0 (source), r1 (router), r2 (router), n3 (sink)
  NodeContainer nodes;
  nodes.Create(4);

  // Bottleneck link = 500 kbps, 20 ms; edge links = 10 Mbps, 1 ms
  PointToPointHelper bottleneck;
  bottleneck.SetDeviceAttribute("DataRate", StringValue("500kbps"));
  bottleneck.SetChannelAttribute("Delay",  StringValue("20ms"));

  PointToPointHelper edge;
  edge.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
  edge.SetChannelAttribute("Delay",  StringValue("1ms"));

  // Install the devices on each pair of nodes
  NetDeviceContainer d01 = edge.Install(nodes.Get(0), nodes.Get(1));
  NetDeviceContainer d12 = bottleneck.Install(nodes.Get(1), nodes.Get(2));
  NetDeviceContainer d23 = edge.Install(nodes.Get(2), nodes.Get(3));

  // Standard TCP/IP stack on each node
  InternetStackHelper internet;
  internet.Install(nodes);

  // Configure the COBALT queue on router1 -> router2 (the bottleneck)
  TrafficControlHelper tchCobalt;
  tchCobalt.SetRootQueueDisc("ns3::CobaltQueueDisc",
                             "Target",   TimeValue(MilliSeconds(5)),
                             "Interval", TimeValue(MilliSeconds(100)),
                             "MaxSize",  QueueSizeValue(QueueSize(QueueSizeUnit::PACKETS, 100)));
  QueueDiscContainer qdisc = tchCobalt.Install(d12.Get(0));

  // Assign IP addresses on each point-to-point link
  Ipv4AddressHelper ipv4;
  ipv4.SetBase("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer i01 = ipv4.Assign(d01);

  ipv4.SetBase("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer i12 = ipv4.Assign(d12);

  ipv4.SetBase("10.1.3.0", "255.255.255.0");
  Ipv4InterfaceContainer i23 = ipv4.Assign(d23);

  // Populate routes so each node knows how to reach every other node
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  // Create a UDP sink on node 3, listening on port 4000
  uint16_t port = 4000;
  PacketSinkHelper sink("ns3::UdpSocketFactory",
                        InetSocketAddress(Ipv4Address::GetAny(), port));
  ApplicationContainer sinkApp = sink.Install(nodes.Get(3));
  sinkApp.Start(Seconds(0.0));
  sinkApp.Stop(Seconds(simTime));

  // Create a UDP OnOff application on node 0, sending to node 3 at "dataRate"
  // If testType == "Bursty", we use short On periods and longer Off to emulate bursts
  OnOffHelper source("ns3::UdpSocketFactory",
                     InetSocketAddress(i23.GetAddress(1), port));
  if (testType == "Bursty")
  {
    source.SetAttribute("DataRate",   StringValue("100Mbps"));
    source.SetAttribute("PacketSize", UintegerValue(1024));
    source.SetAttribute("OnTime",     StringValue("ns3::ConstantRandomVariable[Constant=0.1]"));
    source.SetAttribute("OffTime",    StringValue("ns3::ConstantRandomVariable[Constant=0.5]"));
  }
  else
  {
    source.SetAttribute("DataRate",   StringValue(dataRate));
    source.SetAttribute("PacketSize", UintegerValue(1024));
    source.SetAttribute("OnTime",     StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    source.SetAttribute("OffTime",    StringValue("ns3::ConstantRandomVariable[Constant=0]"));
  }

  ApplicationContainer sourceApp = source.Install(nodes.Get(0));
  sourceApp.Start(Seconds(1.0));
  sourceApp.Stop(Seconds(simTime - 0.5));

  // If Bursty, add a second source to node 0 with different On/Off times to intensify burstiness
  if (testType == "Bursty")
  {
    OnOffHelper source2("ns3::UdpSocketFactory",
                        InetSocketAddress(i23.GetAddress(1), port));
    source2.SetAttribute("DataRate",   StringValue("80Mbps"));
    source2.SetAttribute("PacketSize", UintegerValue(1024));
    source2.SetAttribute("OnTime",     StringValue("ns3::ConstantRandomVariable[Constant=0.2]"));
    source2.SetAttribute("OffTime",    StringValue("ns3::ConstantRandomVariable[Constant=0.3]"));

    ApplicationContainer sourceApp2 = source2.Install(nodes.Get(0));
    sourceApp2.Start(Seconds(1.5));
    sourceApp2.Stop(Seconds(simTime - 0.5));
  }

  // FlowMonitor collects stats like delay, jitter, packet losses, etc.
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll();
  Ptr<Ipv4FlowClassifier> classifier =
      DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());

  // Decide which TrafficData object we'll fill
  TrafficData* data = nullptr;
  if (testType == "Extreme")
  {
    data = &extremeCongestion;
  }
  else if (testType == "Bursty")
  {
    data = &burstyTraffic;
  }
  else
  {
    data = &lowCongestion;
  }

  // Schedule our five tracking functions so they run periodically and fill in "data"
  Simulator::Schedule(Seconds(0.0), &TrackQueueSize,    qdisc.Get(0), std::ref(data->queueSize));
  Simulator::Schedule(Seconds(0.0), &TrackQueueDelay,   qdisc.Get(0), std::ref(data->queueDelay));
  Simulator::Schedule(Seconds(0.5), &TrackThroughput,   monitor, classifier, std::ref(data->throughput));
  Simulator::Schedule(Seconds(0.0), &TrackDrops,        qdisc.Get(0), std::ref(data->drops),
                      std::ref(data->lastDrops));
  Simulator::Schedule(Seconds(0.0), &TrackTxPackets,    monitor, classifier, std::ref(data->txPackets),
                      std::ref(data->lastTxPackets));

  // Run the simulation for simTime seconds
  Simulator::Stop(Seconds(simTime));
  Simulator::Run();

  // Gather final FlowMonitor stats and print them
  monitor->CheckForLostPackets();
  auto flowStats = monitor->GetFlowStats();

  std::cout << "==================================================\n";
  std::cout << "COBALT UDP Traffic Type: " << testType << "\n";
  std::cout << "==================================================\n";

  double totalDelay       = 0.0;
  double totalJitter      = 0.0;
  uint64_t totalTxPackets = 0;
  uint64_t totalRxPackets = 0;
  uint64_t totalLostPackets = 0;
  uint32_t flowCount      = 0;

  for (auto const& entry : flowStats)
  {
    Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(entry.first);

    std::cout << "Flow " << entry.first << " ("
              << t.sourceAddress << " -> " << t.destinationAddress << ")\n"
              << "  Tx Packets: "   << entry.second.txPackets << "\n"
              << "  Rx Packets: "   << entry.second.rxPackets << "\n"
              << "  Lost Packets: " << entry.second.lostPackets << "\n";

    totalTxPackets   += entry.second.txPackets;
    totalRxPackets   += entry.second.rxPackets;
    totalLostPackets += entry.second.lostPackets;

    if (entry.second.txPackets > 0)
    {
      double dropRate = 100.0 * entry.second.lostPackets / entry.second.txPackets;
      std::cout << "  Drop Rate: " << dropRate << "%\n";
    }

    if (entry.second.rxPackets > 0)
    {
      double meanDelay  = entry.second.delaySum.GetSeconds()  * 1000.0 / entry.second.rxPackets;
      double meanJitter = entry.second.jitterSum.GetSeconds() * 1000.0 / entry.second.rxPackets;

      std::cout << "  Mean Delay: "  << meanDelay  << " ms\n"
                << "  Mean Jitter: " << meanJitter << " ms\n";

      totalDelay  += meanDelay;
      totalJitter += meanJitter;
      flowCount++;
    }
    std::cout << std::endl;
  }

  // Print summary for this scenario
  std::cout << "Summary Statistics:\n"
            << "  Total Tx Packets: "   << totalTxPackets   << "\n"
            << "  Total Rx Packets: "   << totalRxPackets   << "\n"
            << "  Total Lost Packets: " << totalLostPackets << "\n";

  if (totalTxPackets > 0)
  {
    double overallDrop = 100.0 * totalLostPackets / totalTxPackets;
    std::cout << "  Overall Drop Rate: " << overallDrop << "%\n";
  }

  if (flowCount > 0)
  {
    std::cout << "  Average Delay: "  << (totalDelay  / flowCount) << " ms\n"
              << "  Average Jitter: " << (totalJitter / flowCount) << " ms\n";
  }
  std::cout << std::endl;
}

/**
 * Generate synthetic data for Low, Extreme, and Bursty, then produce Gnuplot comparisons.
 *
 * This does not use real simulation outputs—it's purely made-up data to show how to compare
 * these scenarios in a set of .png and .plt files.
 */
void
GenerateComparisonPlots()
{
  // [Same synthetic data code as before, omitted here for brevity]
  
  // For example:
  std::map<float, double> lowQueueSize, extremeQueueSize, burstyQueueSize;
  // [Fill them with synthetic data...]
  // Then call GeneratePlot(...) for queue size, delay, throughput, drop rate, jitter.
}

/**
 * Main entry point for the COBALT UDP simulation.
 *
 * We run three scenarios: Low (1Mbps), Extreme (100Mbps), and Bursty (short on/off).
 * We also generate synthetic comparison plots at the end.
 */
int
main (int argc, char *argv[])
{
  double simTime = 60.0; // default 60 seconds
  CommandLine cmd;
  cmd.AddValue("simTime", "Simulation time in seconds", simTime);
  cmd.Parse(argc, argv);

  // Enable logging at "INFO" level for our simulation component
  LogComponentEnable("CobaltUdpAnalysis", LOG_LEVEL_INFO);

  // Run three scenarios
  RunTest("1Mbps",   "Low",     simTime);
  RunTest("100Mbps", "Extreme", simTime);
  RunTest("Bursty",  "Bursty",  simTime);

  // Generate synthetic comparison plots (not from real data)
  GenerateComparisonPlots();

  return 0;
}
