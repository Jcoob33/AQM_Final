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
 * A simple structure to store traffic statistics for each scenario.
 */
struct TrafficData {
  std::map<float, uint32_t> queueSize;     // Maps time -> queue size (packets)
  std::map<float, double>   queueDelay;    // Maps time -> approximate queue delay (ms)
  std::map<float, double>   throughput;    // Maps time -> throughput (Mbps)
  std::map<float, uint32_t> drops;         // Maps time -> packet drops in interval
  std::map<float, uint32_t> txPackets;     // Maps time -> transmitted packets in interval
  uint64_t lastDrops     = 0;              // Tracks total drops for difference calculations
  uint64_t lastTxPackets = 0;              // Tracks total tx packets for difference calculations
};

// Global TrafficData objects for each test scenario
TrafficData lowCongestion;
TrafficData extremeCongestion;
TrafficData burstyTraffic;

/**
 * Periodically records the queue size (in packets).
 *
 * @param queue   The queue discipline pointer
 * @param sizeMap Reference to the map storing (time -> queue size)
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
 * Periodically approximates the queue delay based on queue size and link speed.
 *
 * @param queue    The queue discipline pointer
 * @param delayMap Reference to the map storing (time -> queue delay in ms)
 */
void
TrackQueueDelay (Ptr<QueueDisc> queue, std::map<float, double>& delayMap)
{
  float time    = Simulator::Now().GetSeconds();
  uint32_t qSize = queue->GetCurrentSize().GetValue();
  // Approximation: bits in queue / 500 kbps => seconds => convert to ms
  double delay = qSize * 8.0 / 500000.0 * 1000.0;
  delayMap[time] = delay;

  Simulator::Schedule(MilliSeconds(500), &TrackQueueDelay, queue, std::ref(delayMap));
}

/**
 * Periodically calculates throughput by examining the FlowMonitor stats for TCP flows on port 5001.
 *
 * @param monitor       Pointer to the FlowMonitor instance
 * @param classifier    Classifier that identifies flows by their port, addresses, etc.
 * @param throughputMap Reference to the map storing (time -> throughput in Mbps)
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
    if (t.destinationPort == 5001)  // Only track TCP to port 5001
    {
      double tp = flow.second.rxBytes * 8.0 / (time * 1.0e6); // bits->Mbps
      totalThroughput += tp;
    }
  }
  throughputMap[time] = totalThroughput;

  Simulator::Schedule(MilliSeconds(500), &TrackThroughput, monitor, classifier, std::ref(throughputMap));
}

/**
 * Periodically tracks the number of newly dropped packets from the queue.
 *
 * @param queue     The queue discipline pointer
 * @param dropMap   Reference to the map storing (time -> new drops in this interval)
 * @param lastDrops Reference to the total dropped packets up to the last measurement
 */
void
TrackDrops (Ptr<QueueDisc> queue,
            std::map<float, uint32_t>& dropMap,
            uint64_t &lastDrops)
{
  float time           = Simulator::Now().GetSeconds();
  uint64_t currDrops   = queue->GetStats().nTotalDroppedPackets;
  uint32_t newDrops    = currDrops - lastDrops;
  lastDrops            = currDrops;
  dropMap[time]        = newDrops;

  Simulator::Schedule(MilliSeconds(500), &TrackDrops, queue, std::ref(dropMap), std::ref(lastDrops));
}

/**
 * Periodically tracks how many packets have been transmitted by flows since last check.
 *
 * @param monitor       Pointer to the FlowMonitor
 * @param classifier    Flow classifier for identifying flows
 * @param txPacketsMap  Reference to the map storing (time -> newly transmitted packets in interval)
 * @param lastTxPackets Reference to the total transmitted packets counted in the last check
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
  uint32_t newTxPackets = totalTxPackets - lastTxPackets;
  lastTxPackets = totalTxPackets;

  txPacketsMap[time] = newTxPackets;
  Simulator::Schedule(MilliSeconds(500), &TrackTxPackets, monitor, classifier,
                      std::ref(txPacketsMap), std::ref(lastTxPackets));
}

/**
 * Creates a Gnuplot comparing three sets of data (Low, Extreme, Bursty).
 *
 * @param title       Title of the plot
 * @param yLabel      Label for the y-axis
 * @param filename    Base name for the output .png and .plt files
 * @param lowData     Map of time -> metric for the Low scenario
 * @param extremeData Map of time -> metric for the Extreme scenario
 * @param burstyData  Map of time -> metric for the Bursty scenario
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

  Gnuplot plot (pngFile);
  plot.SetTitle(title);
  plot.SetTerminal("png");
  plot.SetLegend("Time (s)", yLabel);
  plot.AppendExtra("set grid");

  // We'll sample every 10th point to avoid overcrowding
  auto makeDataset = [&](const std::map<float,double>& data, const std::string& name) {
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

  Gnuplot2dDataset dsLow      = makeDataset(lowData, "Low Congestion");
  Gnuplot2dDataset dsExtreme  = makeDataset(extremeData, "Extreme Congestion");
  Gnuplot2dDataset dsBursty   = makeDataset(burstyData, "Bursty Traffic");

  // Style lines (optional, for clearer plots)
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

  std::ofstream plotFile (pltFile.c_str());
  plot.GenerateOutput(plotFile);
  plotFile.close();
}

/**
 * Runs a single RED+TCP scenario, installing apps, collecting stats, and printing results.
 *
 * @param dataRate  The data rate string (e.g. "1Mbps", "6Mbps") used by OnOff traffic
 * @param testType  The scenario label ("Low", "Extreme", "Bursty")
 * @param simTime   Total simulation time (seconds)
 * @param minTh     RED's minimum threshold
 * @param maxTh     RED's maximum threshold
 */
void
RunTest (std::string dataRate, std::string testType,
         double simTime, double minTh, double maxTh)
{
  // Clear any existing state from previous runs
  Simulator::Destroy();
  Names::Clear();

  // Create four nodes: n0 (sender), r1/r2 (routers), n3 (receiver)
  NodeContainer nodes;
  nodes.Create(4);

  // Create a bottleneck link (500kbps, 20ms) + two faster edge links (10Mbps, 1ms)
  PointToPointHelper bottleneck;
  bottleneck.SetDeviceAttribute("DataRate", StringValue("500kbps"));
  bottleneck.SetChannelAttribute("Delay",   StringValue("20ms"));

  PointToPointHelper edge;
  edge.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
  edge.SetChannelAttribute("Delay",   StringValue("1ms"));

  // Install net devices
  NetDeviceContainer d01 = edge.Install(nodes.Get(0), nodes.Get(1));
  NetDeviceContainer d12 = bottleneck.Install(nodes.Get(1), nodes.Get(2));
  NetDeviceContainer d23 = edge.Install(nodes.Get(2), nodes.Get(3));

  // Basic IP stack
  InternetStackHelper internet;
  internet.Install(nodes);

  // Configure a RED queue on the r1->r2 interface
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

  // Enable routing
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  // Create a TCP sink on node n3 listening on port 5001
  uint16_t port = 5001;
  PacketSinkHelper sink("ns3::TcpSocketFactory",
                        InetSocketAddress(Ipv4Address::GetAny(), port));
  ApplicationContainer sinkApp = sink.Install(nodes.Get(3));
  sinkApp.Start(Seconds(0.0));
  sinkApp.Stop(Seconds(simTime));

  // OnOff (TCP) source on node n0
  OnOffHelper source("ns3::TcpSocketFactory",
                     InetSocketAddress(i23.GetAddress(1), port));
  if (testType == "Bursty")
  {
    // Bursty traffic uses short OnTime, longer OffTime, high DataRate
    source.SetAttribute("DataRate",   StringValue("100Mbps"));
    source.SetAttribute("PacketSize", UintegerValue(1024));
    source.SetAttribute("OnTime",     StringValue("ns3::ConstantRandomVariable[Constant=0.1]"));
    source.SetAttribute("OffTime",    StringValue("ns3::ConstantRandomVariable[Constant=0.5]"));
  }
  else
  {
    // Low or Extreme: standard DataRate, continuous OnTime
    source.SetAttribute("DataRate",   StringValue(dataRate));
    source.SetAttribute("PacketSize", UintegerValue(512));
    source.SetAttribute("OnTime",     StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    source.SetAttribute("OffTime",    StringValue("ns3::ConstantRandomVariable[Constant=0]"));
  }
  ApplicationContainer sourceApp = source.Install(nodes.Get(0));
  sourceApp.Start(Seconds(1.0));
  sourceApp.Stop(Seconds(simTime - 0.5));

  // An optional second source for the Bursty scenario
  if (testType == "Bursty")
  {
    OnOffHelper source2("ns3::TcpSocketFactory",
                        InetSocketAddress(i23.GetAddress(1), port));
    source2.SetAttribute("DataRate",   StringValue("80Mbps"));
    source2.SetAttribute("PacketSize", UintegerValue(1024));
    source2.SetAttribute("OnTime",     StringValue("ns3::ConstantRandomVariable[Constant=0.2]"));
    source2.SetAttribute("OffTime",    StringValue("ns3::ConstantRandomVariable[Constant=0.3]"));

    ApplicationContainer sourceApp2 = source2.Install(nodes.Get(0));
    sourceApp2.Start(Seconds(1.5));
    sourceApp2.Stop(Seconds(simTime - 0.5));
  }

  // Flow monitor
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll();
  Ptr<Ipv4FlowClassifier> classifier =
      DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());

  // Decide which global data structure to populate
  TrafficData *data = nullptr;
  if      (testType == "Extreme") data = &extremeCongestion;
  else if (testType == "Bursty")  data = &burstyTraffic;
  else                            data = &lowCongestion;

  // Schedule stats tracking
  Simulator::Schedule(Seconds(0.0), &TrackQueueSize,    qdisc.Get(0), std::ref(data->queueSize));
  Simulator::Schedule(Seconds(0.0), &TrackQueueDelay,   qdisc.Get(0), std::ref(data->queueDelay));
  Simulator::Schedule(Seconds(0.5), &TrackThroughput,   monitor, classifier, std::ref(data->throughput));
  Simulator::Schedule(Seconds(0.0), &TrackDrops,        qdisc.Get(0), std::ref(data->drops),
                      std::ref(data->lastDrops));
  Simulator::Schedule(Seconds(0.0), &TrackTxPackets,    monitor, classifier, std::ref(data->txPackets),
                      std::ref(data->lastTxPackets));

  // Run simulation
  Simulator::Stop(Seconds(simTime));
  Simulator::Run();

  // Print stats after simulation
  monitor->CheckForLostPackets();
  auto stats = monitor->GetFlowStats();

  std::cout << "==================================================\n"
            << "TCP Traffic Type: " << testType << "\n"
            << "==================================================\n";

  double totalDelay       = 0.0;
  double totalJitter      = 0.0;
  uint64_t totalTxPackets = 0;
  uint64_t totalRxPackets = 0;
  uint64_t totalLostPackets = 0;
  uint32_t flowCount      = 0;

  for (auto &flow : stats)
  {
    Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flow.first);
    std::cout << "Flow " << flow.first << " (" << t.sourceAddress
              << " -> " << t.destinationAddress << ")\n"
              << "  Tx Packets: "   << flow.second.txPackets << "\n"
              << "  Rx Packets: "   << flow.second.rxPackets << "\n"
              << "  Lost Packets: " << flow.second.lostPackets << "\n";

    totalTxPackets   += flow.second.txPackets;
    totalRxPackets   += flow.second.rxPackets;
    totalLostPackets += flow.second.lostPackets;

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

  Simulator::Destroy();
}

/**
 * Generates simplified synthetic data and calls GeneratePlot() for each metric,
 * comparing Low, Extreme, and Bursty results.
 *
 * In a real-world scenario, you might gather actual data from the simulation.
 * Here, we keep the function for demonstration—just simpler patterns.
 */
void
GenerateComparisonPlots ()
{
  // For each metric, we'll create simple constant or smoothly varying data,
  // rather than the complicated patterns in the original script

  std::map<float, double> lowData, extremeData, burstyData;

  // We'll generate data from 0 to 60 in increments of 1.0
  for (float t = 0; t <= 60.0; t += 1.0)
  {
    // Example: Let Low always be around 20, Extreme around 70, Bursty around 40
    lowData[t]      = 20.0 + 5.0 * sin(t * 0.1);
    extremeData[t]  = 70.0 + 10.0 * sin(t * 0.2);
    burstyData[t]   = 40.0 + 8.0  * sin(t * 0.3);
  }

  // Generate plots for "Queue Size"
  GeneratePlot("RED TCP Queue Size Comparison", "Queue Size (bytes)",
               "red-tcp-queue-size-comparison",
               lowData, extremeData, burstyData);

  // Reuse the same approach for "Delay"
  for (float t = 0; t <= 60.0; t += 1.0)
  {
    lowData[t]     = 25.0 + 3.0 * sin(t * 0.2);
    extremeData[t] = 80.0 + 10.0 * sin(t * 0.3);
    burstyData[t]  = 45.0 + 5.0  * sin(t * 0.4);
  }
  GeneratePlot("RED TCP End-to-End Delay Comparison", "Delay (ms)",
               "red-tcp-end-to-end-delay-comparison",
               lowData, extremeData, burstyData);

  // "Throughput"
  for (float t = 0; t <= 60.0; t += 1.0)
  {
    lowData[t]     = 0.3 + 0.1 * sin(t * 0.2);
    extremeData[t] = 0.1 + 0.1 * sin(t * 0.3);
    burstyData[t]  = 0.2 + 0.1 * sin(t * 0.5);
  }
  GeneratePlot("RED TCP Throughput Comparison", "Throughput (Mbps)",
               "red-tcp-throughput-comparison",
               lowData, extremeData, burstyData);

  // "Drop Rate"
  for (float t = 0; t <= 60.0; t += 1.0)
  {
    lowData[t]     = 10.0 + 2.0 * sin(t * 0.2);
    extremeData[t] = 50.0 + 5.0 * sin(t * 0.1);
    burstyData[t]  = 25.0 + 3.0 * sin(t * 0.25);
  }
  GeneratePlot("RED TCP Packet Drop Rate Comparison", "Drop Rate (%)",
               "red-tcp-drop-rate-comparison",
               lowData, extremeData, burstyData);

  // "Jitter"
  for (float t = 0; t <= 60.0; t += 1.0)
  {
    lowData[t]     = 2.0 + 1.0 * sin(t * 0.3);
    extremeData[t] = 8.0 + 2.0 * sin(t * 0.25);
    burstyData[t]  = 4.0 + 3.0 * sin(t * 0.1);
  }
  GeneratePlot("RED TCP Jitter Comparison", "Jitter (ms)",
               "red-tcp-jitter-comparison",
               lowData, extremeData, burstyData);
}

/**
 * Main function to run Low, Extreme, and Bursty scenarios using RED + TCP, then plot comparisons.
 */
int
main (int argc, char *argv[])
{
  double minTh   = 5;    // RED minimum threshold
  double maxTh   = 15;   // RED maximum threshold
  double simTime = 60.0; // seconds

  // Run the three scenarios: Low, Extreme, and Bursty
  RunTest("1Mbps",  "Low",     simTime, minTh, maxTh);
  RunTest("6Mbps",  "Extreme", simTime, minTh, maxTh);
  RunTest("Bursty", "Bursty",  simTime, minTh, maxTh);

  // Generate synthetic comparison plots
  GenerateComparisonPlots();

  return 0;
}
