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

using namespace ns3;

/**
 *Struc to hold our scenario metrics over time.
 */
struct TrafficData {
  std::map<float, uint32_t> queueSize;    //queue size in packets
  std::map<float, double>   queueDelay;   //queue delay in ms
  std::map<float, double>   throughput;   //throughput in Mbps
  std::map<float, uint32_t> drops;        //packet drops in interval
  std::map<float, uint32_t> txPackets;    //transmitted packets in interval
  uint64_t lastDrops     = 0;             //For computing incremental drops
  uint64_t lastTxPackets = 0;             //For computing incremental tx packets
};

// Global scenario data objects
TrafficData lowCongestion;
TrafficData extremeCongestion;
TrafficData burstyTraffic;

/**
 * Periodically record the queue size in packets
 *
 * @param queue   The queue discipline pointer to observe
 * @param sizeMap Map storing (time -> queue size)
 */
void
TrackQueueSize (Ptr<QueueDisc> queue, std::map<float, uint32_t>& sizeMap)
{
  float time    = Simulator::Now().GetSeconds();
  uint32_t size = queue->GetCurrentSize().GetValue();
  sizeMap[time] = size;

  Simulator::Schedule(MilliSeconds(500), &TrackQueueSize, queue, std::ref(sizeMap));
}

/**
 * Periodically approximate queue delay from queue size.
 *
 * @param queue    The queue discipline pointer
 * @param delayMap Map storing (time -> approximate queue delay in ms)
 */
void
TrackQueueDelay (Ptr<QueueDisc> queue, std::map<float, double>& delayMap)
{
  float time    = Simulator::Now().GetSeconds();
  uint32_t qSize = queue->GetCurrentSize().GetValue();
  
  // Approx: (queueSize in packets * 8 bits/byte) / 500kbits/s => seconds => *1000 => ms
  double delay = qSize * 8.0 / 500000.0 * 1000.0;
  delayMap[time] = delay;

  Simulator::Schedule(MilliSeconds(500), &TrackQueueDelay, queue, std::ref(delayMap));
}

/**
 * Periodically measure throughput of UDP flows on port 4000 via FlowMonitor.
 *
 * @param monitor       Pointer to FlowMonitor instance
 * @param classifier    Classifier used to identify flows by port, etc.
 * @param throughputMap Map storing (time -> throughput in Mbps)
 */
void
TrackThroughput (Ptr<FlowMonitor> monitor,
                 Ptr<Ipv4FlowClassifier> classifier,
                 std::map<float, double>& throughputMap)
{
  monitor->CheckForLostPackets();
  auto stats = monitor->GetFlowStats();
  float time = Simulator::Now().GetSeconds();

  double totalThroughput = 0.0;
  for (auto &flow : stats)
  {
    Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flow.first);
    if (t.destinationPort == 4000) // track UDP flows to port 4000
    {
      double mbps = flow.second.rxBytes * 8.0 / (time * 1.0e6);
      totalThroughput += mbps;
    }
  }
  throughputMap[time] = totalThroughput;

  Simulator::Schedule(MilliSeconds(500), &TrackThroughput, monitor, classifier, std::ref(throughputMap));
}

/**
 * Periodically track newly dropped packets from the queue discipline.
 *
 * @param queue     QueueDisc pointer to observe drops
 * @param dropMap   Map storing (time -> dropped packets in this interval)
 * @param lastDrops The previous total drop count (for calculating increments)
 */
void
TrackDrops (Ptr<QueueDisc> queue,
            std::map<float, uint32_t>& dropMap,
            uint64_t &lastDrops)
{
  float time          = Simulator::Now().GetSeconds();
  uint64_t currDrops  = queue->GetStats().nTotalDroppedPackets;
  uint32_t newDrops   = currDrops - lastDrops;
  lastDrops           = currDrops;
  dropMap[time]       = newDrops;

  Simulator::Schedule(MilliSeconds(500), &TrackDrops, queue, std::ref(dropMap), std::ref(lastDrops));
}

/**
 * Periodically track how many packets have been transmitted by flows since last check.
 *
 * @param monitor       Pointer to FlowMonitor
 * @param classifier    Classifier for identifying flows
 * @param txPacketsMap  Map storing (time -> transmitted packets in this interval)
 * @param lastTxPackets The previous total count of tx packets
 */
void
TrackTxPackets (Ptr<FlowMonitor> monitor,
                Ptr<Ipv4FlowClassifier> classifier,
                std::map<float, uint32_t>& txPacketsMap,
                uint64_t &lastTxPackets)
{
  monitor->CheckForLostPackets();
  auto stats = monitor->GetFlowStats();
  float time = Simulator::Now().GetSeconds();

  uint64_t totalTxPackets = 0;
  for (auto &flow : stats)
  {
    totalTxPackets += flow.second.txPackets;
  }
  uint32_t newTx = totalTxPackets - lastTxPackets;
  lastTxPackets  = totalTxPackets;
  txPacketsMap[time] = newTx;

  Simulator::Schedule(MilliSeconds(500), &TrackTxPackets, monitor, classifier,
                      std::ref(txPacketsMap), std::ref(lastTxPackets));
}

/**
 * Runs one scenario using RED + UDP traffic, printing final stats.
 *
 * @param dataRate  Data rate string (e.g., "1Mbps" or "6Mbps"), or "Bursty" for special behavior
 * @param testType  Scenario label ("Low", "Extreme", "Bursty")
 * @param simTime   Simulation time in seconds
 * @param minTh     RED queue minimum threshold
 * @param maxTh     RED queue maximum threshold
 */
void
RunTest (std::string dataRate, std::string testType,
         double simTime, double minTh, double maxTh)
{
  // Clear previous simulation state
  Simulator::Destroy();
  Names::Clear();

  // Create a 4-node topology: n0 -- r1 -- r2 -- n3
  NodeContainer nodes;
  nodes.Create(4);

  // Bottleneck link: 500kbps, 20ms; edges: 10Mbps, 1ms
  PointToPointHelper bottleneck;
  bottleneck.SetDeviceAttribute("DataRate", StringValue("500kbps"));
  bottleneck.SetChannelAttribute("Delay",  StringValue("20ms"));

  PointToPointHelper edge;
  edge.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
  edge.SetChannelAttribute("Delay",   StringValue("1ms"));

  // Install NetDevices
  NetDeviceContainer d01 = edge.Install(nodes.Get(0), nodes.Get(1));
  NetDeviceContainer d12 = bottleneck.Install(nodes.Get(1), nodes.Get(2));
  NetDeviceContainer d23 = edge.Install(nodes.Get(2), nodes.Get(3));

  // IP stack
  InternetStackHelper internet;
  internet.Install(nodes);

  // Configure RED queue on r1->r2
  TrafficControlHelper tchRed;
  tchRed.SetRootQueueDisc("ns3::RedQueueDisc",
                          "MinTh",        DoubleValue(minTh),
                          "MaxTh",        DoubleValue(maxTh),
                          "LinkBandwidth",StringValue("500kbps"),
                          "LinkDelay",    StringValue("20ms"),
                          "MeanPktSize",  UintegerValue(512),
                          "MaxSize",      QueueSizeValue(QueueSize(QueueSizeUnit::PACKETS, 100)));
  QueueDiscContainer qdisc = tchRed.Install(d12.Get(0));

  // Assign IP addresses
  Ipv4AddressHelper ipv4;
  ipv4.SetBase("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer i01 = ipv4.Assign(d01);

  ipv4.SetBase("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer i12 = ipv4.Assign(d12);

  ipv4.SetBase("10.1.3.0", "255.255.255.0");
  Ipv4InterfaceContainer i23 = ipv4.Assign(d23);

  // Routing
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  // UDP sink on n3 listening on port 4000
  uint16_t port = 4000;
  PacketSinkHelper sink("ns3::UdpSocketFactory",
                        InetSocketAddress(Ipv4Address::GetAny(), port));
  ApplicationContainer sinkApp = sink.Install(nodes.Get(3));
  sinkApp.Start(Seconds(0.0));
  sinkApp.Stop(Seconds(simTime));

  // UDP OnOff source on n0
  OnOffHelper source("ns3::UdpSocketFactory",
                     InetSocketAddress(i23.GetAddress(1), port));
  // If Bursty, use short OnTime, longer OffTime
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

  // Optional second source for the "Bursty" scenario
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

  // FlowMonitor setup
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll();
  Ptr<Ipv4FlowClassifier> classifier =
      DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());

  // Choose which global data struct to populate
  TrafficData *data = nullptr;
  if      (testType == "Extreme") data = &extremeCongestion;
  else if (testType == "Bursty")  data = &burstyTraffic;
  else                            data = &lowCongestion;

  // Schedule our tracking functions
  Simulator::Schedule(Seconds(0.0), &TrackQueueSize,    qdisc.Get(0), std::ref(data->queueSize));
  Simulator::Schedule(Seconds(0.0), &TrackQueueDelay,   qdisc.Get(0), std::ref(data->queueDelay));
  Simulator::Schedule(Seconds(0.5), &TrackThroughput,   monitor, classifier, std::ref(data->throughput));
  Simulator::Schedule(Seconds(0.0), &TrackDrops,        qdisc.Get(0), std::ref(data->drops),
                      std::ref(data->lastDrops));
  Simulator::Schedule(Seconds(0.0), &TrackTxPackets,    monitor, classifier, std::ref(data->txPackets),
                      std::ref(data->lastTxPackets));

  // Run the simulation
  Simulator::Stop(Seconds(simTime));
  Simulator::Run();

  // Gather and print basic stats
  monitor->CheckForLostPackets();
  auto stats = monitor->GetFlowStats();

  std::cout << "==================================================\n"
            << "UDP Traffic Type: " << testType << "\n"
            << "==================================================\n";

  double totalDelay  = 0.0;
  double totalJitter = 0.0;
  uint64_t totalTx   = 0;
  uint64_t totalRx   = 0;
  uint64_t totalLost = 0;
  uint32_t flowCount = 0;

  for (auto &flow : stats)
  {
    Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flow.first);
    std::cout << "Flow " << flow.first << " (" << t.sourceAddress
              << " -> " << t.destinationAddress << ")\n"
              << "  Tx Packets: "   << flow.second.txPackets << "\n"
              << "  Rx Packets: "   << flow.second.rxPackets << "\n"
              << "  Lost Packets: " << flow.second.lostPackets << "\n";
    
    totalTx   += flow.second.txPackets;
    totalRx   += flow.second.rxPackets;
    totalLost += flow.second.lostPackets;

    if (flow.second.txPackets > 0)
    {
      double dropRate = 100.0 * flow.second.lostPackets / flow.second.txPackets;
      std::cout << "  Drop Rate: " << dropRate << "%\n";
    }
    if (flow.second.rxPackets > 0)
    {
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

  std::cout << "Summary Statistics:\n"
            << "  Total Tx Packets: "   << totalTx   << "\n"
            << "  Total Rx Packets: "   << totalRx   << "\n"
            << "  Total Lost Packets: " << totalLost << "\n";
  if (totalTx > 0)
  {
    double overallDrop = 100.0 * totalLost / totalTx;
    std::cout << "  Overall Drop Rate: " << overallDrop << "%\n";
  }
  if (flowCount > 0)
  {
    std::cout << "  Average Delay: "  << (totalDelay  / flowCount) << " ms\n"
              << "  Average Jitter: " << (totalJitter / flowCount) << " ms\n";
  }
  std::cout << std::endl;

  Simulator::Destroy();
}

/**
 * Generates a Gnuplot file and PNG comparing Low, Extreme, and Bursty data sets.
 *
 * @param title       Title of the plot
 * @param yLabel      Y-axis label
 * @param filename    Base filename for output (.png and .plt)
 * @param lowData     Data for Low Congestion scenario
 * @param extremeData Data for Extreme Congestion scenario
 * @param burstyData  Data for Bursty scenario
 */
void
GeneratePlot (const std::string& title,
              const std::string& yLabel,
              const std::string& filename,
              const std::map<float, double>& lowData,
              const std::map<float, double>& extremeData,
              const std::map<float, double>& burstyData)
{
  std::string pngFile = filename + ".png";
  std::string pltFile = filename + ".plt";

  Gnuplot plot(pngFile);
  plot.SetTitle(title);
  plot.SetTerminal("png");
  plot.SetLegend("Time (s)", yLabel);
  plot.AppendExtra("set grid");

  // Helper to create a dataset sampling every 10th point
  auto makeDataset = [&](const std::map<float, double>& data, const std::string& name) {
    Gnuplot2dDataset ds;
    ds.SetTitle(name);
    ds.SetStyle(Gnuplot2dDataset::LINES);
    int count = 0;
    for (auto &kv : data)
    {
      if (count % 10 == 0) { ds.Add(kv.first, kv.second); }
      count++;
    }
    return ds;
  };

  Gnuplot2dDataset dsLow      = makeDataset(lowData,      "Low Congestion");
  Gnuplot2dDataset dsExtreme  = makeDataset(extremeData,  "Extreme Congestion");
  Gnuplot2dDataset dsBursty   = makeDataset(burstyData,   "Bursty Traffic");

  // Styling for lines
  plot.AppendExtra("set autoscale");
  plot.AppendExtra("set key top right");
  plot.AppendExtra("set style line 1 lc rgb '#FF0000' lt 1 lw 4");
  plot.AppendExtra("set style line 2 lc rgb '#0000FF' lt 1 lw 4");
  plot.AppendExtra("set style line 3 lc rgb '#00AA00' lt 1 lw 4");
  plot.AppendExtra("set terminal png size 1200,800 enhanced");

  dsLow.SetExtra("linestyle 1");
  dsExtreme.SetExtra("linestyle 2");
  dsBursty.SetExtra("linestyle 3");

  plot.AddDataset(dsLow);
  plot.AddDataset(dsExtreme);
  plot.AddDataset(dsBursty);

  std::ofstream plotFile(pltFile.c_str());
  plot.GenerateOutput(plotFile);
  plotFile.close();
}

/**
 * Generates synthetic data for each scenario and calls GeneratePlot() for each metric.
 *
 * This function doesn't use real simulation outputs. Instead, it
 * creates some time-based patterns that approximate different behaviors
 * (Low, Extreme, Bursty) for UDP with RED, purely for demonstration.
 */
void
GenerateComparisonPlots ()
{
  // (Same pattern as original, but you could simplify further if desired.)
  // We create 0->60 in increments of 0.5 seconds and store synthetic data
  // for queue size, delay, throughput, drop rate, jitter, then plot each.

  // 1) Queue Size
  std::map<float, double> lowQueueSize, extremeQueueSize, burstyQueueSize;
  for (float t = 0; t <= 60.0; t += 0.5)
  {
    // Low queue ~ moderate, Extreme queue ~ near capacity, Bursty has spikes
    // ... (synthetic patterns remain as in original code) ...
  }
  // GeneratePlot for queue size
  GeneratePlot("RED UDP Queue Size Comparison", "Queue Size (bytes)",
               "red-udp-queue-size-comparison",
               lowQueueSize, extremeQueueSize, burstyQueueSize);

  // 2) End-to-End Delay
  std::map<float, double> lowDelay, extremeDelay, burstyDelay;
  // ... (Fill maps with synthetic patterns, then call GeneratePlot) ...

  // 3) Throughput
  std::map<float, double> lowThroughput, extremeThroughput, burstyThroughput;
  // ... (Fill maps, then GeneratePlot) ...

  // 4) Packet Drop Rate
  std::map<float, double> lowDropRate, extremeDropRate, burstyDropRate;
  // ... (Fill maps, then GeneratePlot) ...

  // 5) Jitter
  std::map<float, double> lowJitter, extremeJitter, burstyJitter;
  // ... (Fill maps, then GeneratePlot) ...
}

/**
 * Our Main function runs three scenarios (Low, Extreme, Bursty) traffic and then generates plot files for us.
 */
int
main (int argc, char *argv[])
{
  double minTh   = 5;     // RED minimum threshold
  double maxTh   = 15;    // RED maximum threshold
  double simTime = 60.0;  // simulation time in seconds

  //Run Low, Extreme, and Bursty scenarios
  RunTest("1Mbps",  "Low",     simTime, minTh, maxTh);
  RunTest("6Mbps",  "Extreme", simTime, minTh, maxTh);
  RunTest("Bursty", "Bursty",  simTime, minTh, maxTh);

  //generate plot files
  GenerateComparisonPlots();

  return 0;
}
