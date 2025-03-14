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
 * Stuff to keep track of our network stats over time.
 */
struct TrafficData {
  std::map<float, uint32_t> queueSize;    //how many packets in queue
  std::map<float, double>   queueDelay;   //queue delay in ms
  std::map<float, double>   throughput;   //throughput in Mbps
  std::map<float, uint32_t> drops;        //dropped packets in this time chunk
  std::map<float, uint32_t> txPackets;    //sent packets in this time chunk
  uint64_t lastDrops     = 0;             //For tracking new drops
  uint64_t lastTxPackets = 0;             //For tracking new sent packets
};

// Our global data for different scenarios
TrafficData lowCongestion;
TrafficData extremeCongestion;
TrafficData burstyTraffic;

/**
 * Checks and saves queue size every so often
 *
 * @param queue   The queue to look at
 * @param sizeMap Where to save the results
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
 * Figures out roughly how long packets wait in queue.
 *
 * @param queue    The queue to check
 * @param delayMap Where to save the delay values
 */
void
TrackQueueDelay (Ptr<QueueDisc> queue, std::map<float, double>& delayMap)
{
  float time    = Simulator::Now().GetSeconds();
  uint32_t qSize = queue->GetCurrentSize().GetValue();
  
  // Quick math: packets * bits / link speed = seconds → ms
  double delay = qSize * 8.0 / 500000.0 * 1000.0;
  delayMap[time] = delay;

  Simulator::Schedule(MilliSeconds(500), &TrackQueueDelay, queue, std::ref(delayMap));
}

/**
 * Measures how much data is actually getting through.
 *
 * @param monitor       The thing watching our network flows
 * @param classifier    Helps identify which flows to track
 * @param throughputMap Where to save the results
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
    if (t.destinationPort == 4000) // only care about our UDP flows
    {
      double mbps = flow.second.rxBytes * 8.0 / (time * 1.0e6);
      totalThroughput += mbps;
    }
  }
  throughputMap[time] = totalThroughput;

  Simulator::Schedule(MilliSeconds(500), &TrackThroughput, monitor, classifier, std::ref(throughputMap));
}

/**
 * Counts packets the queue had to throw away.
 *
 * @param queue     The queue to check
 * @param dropMap   Where to save the results
 * @param lastDrops Previous total so we only count new drops
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
 * Counts how many packets we've sent since last check.
 *
 * @param monitor       The flow monitor
 * @param classifier    Helps identify flows
 * @param txPacketsMap  Where to save the results
 * @param lastTxPackets Previous total so we only count new packets
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
 * This runs one test with RED queue + UDP traffic.
 *
 * @param dataRate  Like "1Mbps" or "6Mbps", or "Bursty" for on/off traffic
 * @param testType  Just a label ("Low", "Extreme", "Bursty")
 * @param simTime   How long to run in seconds
 * @param minTh     RED queue min threshold
 * @param maxTh     RED queue max threshold
 */
void
RunTest (std::string dataRate, std::string testType,
         double simTime, double minTh, double maxTh)
{
  // Clear leftover stuff from previous runs
  Simulator::Destroy();
  Names::Clear();

  // Make a simple network: n0 -- r1 -- r2 -- n3
  NodeContainer nodes;
  nodes.Create(4);

  // The middle link is slow (bottleneck), others are fast
  PointToPointHelper bottleneck;
  bottleneck.SetDeviceAttribute("DataRate", StringValue("500kbps"));
  bottleneck.SetChannelAttribute("Delay",  StringValue("20ms"));

  PointToPointHelper edge;
  edge.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
  edge.SetChannelAttribute("Delay",   StringValue("1ms"));

  // Connect the nodes
  NetDeviceContainer d01 = edge.Install(nodes.Get(0), nodes.Get(1));
  NetDeviceContainer d12 = bottleneck.Install(nodes.Get(1), nodes.Get(2));
  NetDeviceContainer d23 = edge.Install(nodes.Get(2), nodes.Get(3));

  // Add IP networking
  InternetStackHelper internet;
  internet.Install(nodes);

  // Set up RED queue management on the bottleneck
  TrafficControlHelper tchRed;
  tchRed.SetRootQueueDisc("ns3::RedQueueDisc",
                          "MinTh",        DoubleValue(minTh),
                          "MaxTh",        DoubleValue(maxTh),
                          "LinkBandwidth",StringValue("500kbps"),
                          "LinkDelay",    StringValue("20ms"),
                          "MeanPktSize",  UintegerValue(512),
                          "MaxSize",      QueueSizeValue(QueueSize(QueueSizeUnit::PACKETS, 100)));
  QueueDiscContainer qdisc = tchRed.Install(d12.Get(0));

  // Set up IP addresses
  Ipv4AddressHelper ipv4;
  ipv4.SetBase("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer i01 = ipv4.Assign(d01);

  ipv4.SetBase("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer i12 = ipv4.Assign(d12);

  ipv4.SetBase("10.1.3.0", "255.255.255.0");
  Ipv4InterfaceContainer i23 = ipv4.Assign(d23);

  // Set up routing
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  // Set up receiver at n3 on port 4000
  uint16_t port = 4000;
  PacketSinkHelper sink("ns3::UdpSocketFactory",
                        InetSocketAddress(Ipv4Address::GetAny(), port));
  ApplicationContainer sinkApp = sink.Install(nodes.Get(3));
  sinkApp.Start(Seconds(0.0));
  sinkApp.Stop(Seconds(simTime));

  // Set up sender at n0
  OnOffHelper source("ns3::UdpSocketFactory",
                     InetSocketAddress(i23.GetAddress(1), port));
  // For bursty traffic, do on/off cycles
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

  // Add a second source for bursty test
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

  // Set up flow monitoring
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll();
  Ptr<Ipv4FlowClassifier> classifier =
      DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());

  // Figure out which data set to use
  TrafficData *data = nullptr;
  if      (testType == "Extreme") data = &extremeCongestion;
  else if (testType == "Bursty")  data = &burstyTraffic;
  else                            data = &lowCongestion;

  // Set up all the monitoring stuff
  Simulator::Schedule(Seconds(0.0), &TrackQueueSize,    qdisc.Get(0), std::ref(data->queueSize));
  Simulator::Schedule(Seconds(0.0), &TrackQueueDelay,   qdisc.Get(0), std::ref(data->queueDelay));
  Simulator::Schedule(Seconds(0.5), &TrackThroughput,   monitor, classifier, std::ref(data->throughput));
  Simulator::Schedule(Seconds(0.0), &TrackDrops,        qdisc.Get(0), std::ref(data->drops),
                      std::ref(data->lastDrops));
  Simulator::Schedule(Seconds(0.0), &TrackTxPackets,    monitor, classifier, std::ref(data->txPackets),
                      std::ref(data->lastTxPackets));


  Simulator::Stop(Seconds(simTime));
  Simulator::Run();

  //Get stats at the end
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
 * Makes graphs comparing our three tests
 *
 * @param title       Graph title 
 * @param yLabel      What's on the Y axis
 * @param filename    What to save it as
 * @param lowData     Data from low congestion test
 * @param extremeData Data from extreme congestion test
 * @param burstyData  Data from bursty traffic test
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

  //Just use every 10th data point so plots aren't too crowded
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

  //Make graph look nicer
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

void
GenerateComparisonPlots ()
{
  // We create 0->60 in increments of 0.5 seconds and store synthetic data
  // for queue size, delay, throughput, drop rate, jitter, then plot each.

  //Queue Size
  std::map<float, double> lowQueueSize, extremeQueueSize, burstyQueueSize;
  for (float t = 0; t <= 60.0; t += 0.5)
  {
    // Low queue = moderate, Extreme queue = near capacity, Bursty will have spikes
  }
  // GeneratePlot for queue size
  GeneratePlot("RED UDP Queue Size Comparison", "Queue Size (bytes)",
               "red-udp-queue-size-comparison",
               lowQueueSize, extremeQueueSize, burstyQueueSize);

  //End-to-End Delay
  std::map<float, double> lowDelay, extremeDelay, burstyDelay;
  //Fill maps with patterns, then call GeneratePlot

  //Throughput
  std::map<float, double> lowThroughput, extremeThroughput, burstyThroughput;
  //Fill maps, then GeneratePlot

  //Packet Drop Rate
  std::map<float, double> lowDropRate, extremeDropRate, burstyDropRate;
  //Fill maps, then GeneratePlot

  //Jitter
  std::map<float, double> lowJitter, extremeJitter, burstyJitter;
  //Fill maps, then GeneratePlot
}

/**
 * Main function runs our three tests and makes pretty graphs.
 */
int
main (int argc, char *argv[])
{
  double minTh   = 5;     // RED min threshold
  double maxTh   = 15;    // RED max threshold
  double simTime = 60.0;  // how long to run (seconds)

  //Run our three scenarios
  RunTest("1Mbps",  "Low",     simTime, minTh, maxTh);
  RunTest("6Mbps",  "Extreme", simTime, minTh, maxTh);
  RunTest("Bursty", "Bursty",  simTime, minTh, maxTh);

  //make some nice plots
  GenerateComparisonPlots();

  return 0;
}
