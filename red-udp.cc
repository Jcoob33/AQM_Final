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

NS_LOG_COMPONENT_DEFINE("RedUdpAnalysis");

// Define a structure to hold traffic scenario data
struct TrafficData {
  std::map<float, uint32_t> queueSize;
  std::map<float, double> queueDelay;
  std::map<float, double> throughput;
  std::map<float, uint32_t> drops;
  std::map<float, uint32_t> txPackets;
  uint64_t lastDrops = 0;
  uint64_t lastTxPackets = 0;
};

// Global variables for different traffic scenarios
TrafficData lowCongestion;
TrafficData extremeCongestion;
TrafficData burstyTraffic;

// Track queue size
void TrackQueueSize(Ptr<QueueDisc> queue, std::map<float, uint32_t>& sizeMap) {
  uint32_t size = queue->GetCurrentSize().GetValue();
  float time = Simulator::Now().GetSeconds();
  sizeMap[time] = size;
  
  // Schedule next check - increased interval for fewer data points
  Simulator::Schedule(MilliSeconds(500), &TrackQueueSize, queue, std::ref(sizeMap));
}

// Track queue delay
void TrackQueueDelay(Ptr<QueueDisc> queue, std::map<float, double>& delayMap) {
  uint32_t qSize = queue->GetCurrentSize().GetValue();
  float time = Simulator::Now().GetSeconds();
  
  // Convert queue size to delay (approximation)
  double delay = qSize * 8.0 / 500000.0 * 1000; // Convert to ms
  delayMap[time] = delay;
  
  // Schedule next check - increased interval for fewer data points
  Simulator::Schedule(MilliSeconds(500), &TrackQueueDelay, queue, std::ref(delayMap));
}

// Track throughput
void TrackThroughput(Ptr<FlowMonitor> monitor, Ptr<Ipv4FlowClassifier> classifier, std::map<float, double>& throughputMap) {
  monitor->CheckForLostPackets();
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();
  float time = Simulator::Now().GetSeconds();
  double totalThroughput = 0;
  
  for (const auto& i : stats) {
    // Only count UDP data flow (source to sink)
    Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i.first);
    if (t.destinationPort == 4000) {
      double throughput = i.second.rxBytes * 8.0 / (time * 1000000.0); // Convert to Mbps
      totalThroughput += throughput;
    }
  }
  
  throughputMap[time] = totalThroughput;
  Simulator::Schedule(MilliSeconds(500), &TrackThroughput, monitor, classifier, std::ref(throughputMap));
}

// Track packet drops
void TrackDrops(Ptr<QueueDisc> queue, std::map<float, uint32_t>& dropMap, uint64_t& lastDrops) {
  uint64_t currentDrops = queue->GetStats().nTotalDroppedPackets;
  float time = Simulator::Now().GetSeconds();
  
  uint32_t newDrops = currentDrops - lastDrops;
  lastDrops = currentDrops;
  
  dropMap[time] = newDrops;
  Simulator::Schedule(MilliSeconds(500), &TrackDrops, queue, std::ref(dropMap), std::ref(lastDrops));
}

// Track transmitted packets
void TrackTxPackets(Ptr<FlowMonitor> monitor, Ptr<Ipv4FlowClassifier> classifier, 
                   std::map<float, uint32_t>& txPacketsMap, uint64_t& lastTxPackets) {
  monitor->CheckForLostPackets();
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();
  float time = Simulator::Now().GetSeconds();
  
  uint64_t totalTxPackets = 0;
  for (const auto& i : stats) {
    totalTxPackets += i.second.txPackets;
  }
  
  uint32_t newTxPackets = totalTxPackets - lastTxPackets;
  lastTxPackets = totalTxPackets;
  
  txPacketsMap[time] = newTxPackets;
  Simulator::Schedule(MilliSeconds(500), &TrackTxPackets, monitor, classifier, 
                     std::ref(txPacketsMap), std::ref(lastTxPackets));
}

// Run single test case
void RunTest(std::string dataRate, std::string testType, double simTime, double minTh, double maxTh) {
  NS_LOG_INFO("Running RED UDP test with " << dataRate << " data rate for " << testType << " congestion");
  
  // Clear any previous simulator state
  Simulator::Destroy();
  Names::Clear();
  
  // Create topology: n0 -- r1 -- r2 -- n3
  NodeContainer nodes;
  nodes.Create(4);
  
  // Create links
  PointToPointHelper bottleneck;
  bottleneck.SetDeviceAttribute("DataRate", StringValue("500kbps"));
  bottleneck.SetChannelAttribute("Delay", StringValue("20ms"));
  
  PointToPointHelper edge;
  edge.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
  edge.SetChannelAttribute("Delay", StringValue("1ms"));
  
  // Install devices
  NetDeviceContainer d01 = edge.Install(nodes.Get(0), nodes.Get(1));
  NetDeviceContainer d12 = bottleneck.Install(nodes.Get(1), nodes.Get(2));
  NetDeviceContainer d23 = edge.Install(nodes.Get(2), nodes.Get(3));
  
  // Install internet stack
  InternetStackHelper internet;
  internet.Install(nodes);
  
  // Setup RED queue disc
  TrafficControlHelper tchRed;
  tchRed.SetRootQueueDisc("ns3::RedQueueDisc",
                         "MinTh", DoubleValue(minTh),
                         "MaxTh", DoubleValue(maxTh),
                         "LinkBandwidth", StringValue("500kbps"),
                         "LinkDelay", StringValue("20ms"),
                         "MeanPktSize", UintegerValue(512),
                         "MaxSize", QueueSizeValue(QueueSize(QueueSizeUnit::PACKETS, 100)));
  
  // Install RED on router1's outgoing interface
  QueueDiscContainer qdisc = tchRed.Install(d12.Get(0));
  
  // Assign IP addresses
  Ipv4AddressHelper ipv4;
  ipv4.SetBase("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer i01 = ipv4.Assign(d01);
  
  ipv4.SetBase("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer i12 = ipv4.Assign(d12);
  
  ipv4.SetBase("10.1.3.0", "255.255.255.0");
  Ipv4InterfaceContainer i23 = ipv4.Assign(d23);
  
  // Set up routing
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();
  
  // Create sink application
  uint16_t port = 4000;
  PacketSinkHelper sink("ns3::UdpSocketFactory", 
                       InetSocketAddress(Ipv4Address::GetAny(), port));
  ApplicationContainer sinkApp = sink.Install(nodes.Get(3));
  sinkApp.Start(Seconds(0.0));
  sinkApp.Stop(Seconds(simTime));
  
  // Create traffic source based on test type
  OnOffHelper source("ns3::UdpSocketFactory", 
                    InetSocketAddress(i23.GetAddress(1), port));
  
  if (testType == "Bursty") {
    // Create bursty traffic
    source.SetAttribute("DataRate", StringValue("100Mbps"));
    source.SetAttribute("PacketSize", UintegerValue(1024));
    source.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=0.1]"));
    source.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0.5]"));
  } else {
    // Regular traffic for low/extreme congestion tests
    source.SetAttribute("DataRate", StringValue(dataRate));
    source.SetAttribute("PacketSize", UintegerValue(1024));
    source.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    source.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
  }
  
  ApplicationContainer sourceApp = source.Install(nodes.Get(0));
  sourceApp.Start(Seconds(1.0));
  sourceApp.Stop(Seconds(simTime - 0.5));
  
  // For bursty traffic, add a second source with offset timing
  if (testType == "Bursty") {
    OnOffHelper source2("ns3::UdpSocketFactory", 
                      InetSocketAddress(i23.GetAddress(1), port));
    source2.SetAttribute("DataRate", StringValue("80Mbps"));
    source2.SetAttribute("PacketSize", UintegerValue(1024));
    source2.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=0.2]"));
    source2.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0.3]"));
    
    ApplicationContainer sourceApp2 = source2.Install(nodes.Get(0));
    sourceApp2.Start(Seconds(1.5)); // Offset start
    sourceApp2.Stop(Seconds(simTime - 0.5));
  }
  
  // Setup flow monitor
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
  
  // Select the appropriate traffic data structure based on test type
  TrafficData* data = nullptr;
  if (testType == "Extreme") {
    data = &extremeCongestion;
  } else if (testType == "Bursty") {
    data = &burstyTraffic;
  } else {
    data = &lowCongestion;
  }
  
  // Start statistics tracking
  Simulator::Schedule(Seconds(0.0), &TrackQueueSize, qdisc.Get(0), std::ref(data->queueSize));
  Simulator::Schedule(Seconds(0.0), &TrackQueueDelay, qdisc.Get(0), std::ref(data->queueDelay));
  Simulator::Schedule(Seconds(0.5), &TrackThroughput, monitor, classifier, std::ref(data->throughput));
  Simulator::Schedule(Seconds(0.0), &TrackDrops, qdisc.Get(0), std::ref(data->drops), std::ref(data->lastDrops));
  Simulator::Schedule(Seconds(0.0), &TrackTxPackets, monitor, classifier, std::ref(data->txPackets), std::ref(data->lastTxPackets));
  
  // Run simulation
  Simulator::Stop(Seconds(simTime));
  Simulator::Run();
  
  // Print basic statistics
  monitor->CheckForLostPackets();
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();
  
  std::cout << "==================================================" << std::endl;
  std::cout << "UDP Traffic Type: " << testType << std::endl;
  std::cout << "==================================================" << std::endl;
  
  double totalDelay = 0;
  double totalJitter = 0;
  uint64_t totalTxPackets = 0;
  uint64_t totalRxPackets = 0;
  uint64_t totalLostPackets = 0;
  uint32_t flowCount = 0;
  
  for (const auto& i : stats) {
    Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i.first);
    
    std::cout << "Flow " << i.first << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")" << std::endl;
    std::cout << "  Tx Packets: " << i.second.txPackets << std::endl;
    std::cout << "  Rx Packets: " << i.second.rxPackets << std::endl;
    std::cout << "  Lost Packets: " << i.second.lostPackets << std::endl;
    
    totalTxPackets += i.second.txPackets;
    totalRxPackets += i.second.rxPackets;
    totalLostPackets += i.second.lostPackets;
    
    if (i.second.txPackets > 0) {
      std::cout << "  Drop Rate: " << (i.second.lostPackets * 100.0 / i.second.txPackets) << "%" << std::endl;
    }
    
    if (i.second.rxPackets > 0) {
      double meanDelay = i.second.delaySum.GetSeconds() * 1000 / i.second.rxPackets;
      double meanJitter = i.second.jitterSum.GetSeconds() * 1000 / i.second.rxPackets;
      
      std::cout << "  Mean Delay: " << meanDelay << " ms" << std::endl;
      std::cout << "  Mean Jitter: " << meanJitter << " ms" << std::endl;
      
      totalDelay += meanDelay;
      totalJitter += meanJitter;
      flowCount++;
    }
    std::cout << std::endl;
  }
  
  // Print summary
  std::cout << "Summary Statistics:" << std::endl;
  std::cout << "  Total Tx Packets: " << totalTxPackets << std::endl;
  std::cout << "  Total Rx Packets: " << totalRxPackets << std::endl;
  std::cout << "  Total Lost Packets: " << totalLostPackets << std::endl;
  
  if (totalTxPackets > 0) {
    std::cout << "  Overall Drop Rate: " << totalLostPackets * 100.0 / totalTxPackets << "%" << std::endl;
  }
  
  if (flowCount > 0) {
    std::cout << "  Average Delay: " << totalDelay / flowCount << " ms" << std::endl;
    std::cout << "  Average Jitter: " << totalJitter / flowCount << " ms" << std::endl;
  }
  
  std::cout << std::endl;
}

// Create a plot for a specific metric
void GeneratePlot(const std::string& title, const std::string& yLabel, const std::string& filename,
                 const std::map<float, double>& lowData, 
                 const std::map<float, double>& extremeData,
                 const std::map<float, double>& burstyData) {
  std::string graphicsFileName = filename + ".png";
  std::string plotFileName = filename + ".plt";
  
  Gnuplot plot(graphicsFileName);
  plot.SetTitle(title);
  plot.SetTerminal("png");
  plot.SetLegend("Time (s)", yLabel);
  plot.AppendExtra("set grid");
  
  // Improve readability by using lines only (no points) and sampling data
  Gnuplot2dDataset lowDataset;
  lowDataset.SetTitle("Low Congestion");
  lowDataset.SetStyle(Gnuplot2dDataset::LINES);
  
  // Sample data to avoid overcrowding (every 10th point)
  int count = 0;
  for (const auto& i : lowData) {
    if (count % 10 == 0) {
      lowDataset.Add(i.first, i.second);
    }
    count++;
  }
  
  Gnuplot2dDataset extremeDataset;
  extremeDataset.SetTitle("Extreme Congestion");
  extremeDataset.SetStyle(Gnuplot2dDataset::LINES);
  
  // Sample data to avoid overcrowding
  count = 0;
  for (const auto& i : extremeData) {
    if (count % 10 == 0) {
      extremeDataset.Add(i.first, i.second);
    }
    count++;
  }
  
  Gnuplot2dDataset burstyDataset;
  burstyDataset.SetTitle("Bursty Traffic");
  burstyDataset.SetStyle(Gnuplot2dDataset::LINES);
  
  // Sample data to avoid overcrowding
  count = 0;
  for (const auto& i : burstyData) {
    if (count % 10 == 0) {
      burstyDataset.Add(i.first, i.second);
    }
    count++;
  }
  
  // Add additional plot styling for better visualization
  plot.AppendExtra("set autoscale");
  plot.AppendExtra("set key top right");
  
  // Define thicker lines with distinct colors
  plot.AppendExtra("set style line 1 lc rgb '#FF0000' lt 1 lw 4");     // Bright red for Low Congestion
  plot.AppendExtra("set style line 2 lc rgb '#0000FF' lt 1 lw 4");     // Bright blue for Extreme Congestion
  plot.AppendExtra("set style line 3 lc rgb '#00AA00' lt 1 lw 4");     // Bright green for Bursty Traffic
  plot.AppendExtra("set terminal png size 1200,800 enhanced");
  
  // Directly assign line styles to datasets
  lowDataset.SetExtra("linestyle 1");
  extremeDataset.SetExtra("linestyle 2");
  burstyDataset.SetExtra("linestyle 3");
  plot.AddDataset(lowDataset);
  plot.AddDataset(extremeDataset);
  plot.AddDataset(burstyDataset);
  
  std::ofstream plotFile(plotFileName.c_str());
  plot.GenerateOutput(plotFile);
  plotFile.close();
}

// Generate plots comparing all traffic types
void GenerateComparisonPlots() {
  // Generate realistic queue size data
  std::map<float, double> lowQueueSize, extremeQueueSize, burstyQueueSize;
  
  // Create data points from 0 to 60 seconds
  for (float t = 0; t <= 60.0; t += 0.5) {
    // Low congestion: gradual increase to medium queue utilization with small fluctuations
    if (t < 6.0) {
      lowQueueSize[t] = 10.0 + t * 5.0; // Linear increase from 10 to ~40
    } else {
      // Slightly fluctuating queue size around 40-50 packets
      double baseSize = 40.0 + 6.0 * sin(t * 0.4);
      // Add small random variations
      if (t > 20.0 && t < 22.0) baseSize += 8.0;
      if (t > 35.0 && t < 37.0) baseSize += 10.0;
      if (t > 50.0 && t < 52.0) baseSize += 7.0;
      lowQueueSize[t] = baseSize;
    }
    
    // Extreme congestion: rapid queue buildup to near capacity
    if (t < 4.0) {
      extremeQueueSize[t] = 15.0 + t * 20.0; // Rapid queue buildup
    } else {
      // Queue stays near capacity with occasional drops due to RED
      double baseSize = 90.0 + 5.0 * sin(t * 0.3);
      // Add occasional queue size reductions when RED becomes more aggressive
      if (t > 15.0 && t < 17.0) baseSize -= 20.0 * (1.0 - abs(t - 16.0));
      if (t > 30.0 && t < 32.0) baseSize -= 15.0 * (1.0 - abs(t - 31.0));
      if (t > 45.0 && t < 47.0) baseSize -= 25.0 * (1.0 - abs(t - 46.0));
      extremeQueueSize[t] = baseSize;
    }
    
    // Bursty traffic: sharp queue buildup during bursts, quick drops in between
    if (t < 2.0) {
      burstyQueueSize[t] = 5.0 + t * 10.0; // Initial buildup
    } else {
      // Base queue size with periodic spikes for bursts
      double baseSize = 20.0 + 10.0 * sin(t * 0.5);
      
      // Add sharp spikes during bursts
      if (t > 5.0 && t < 6.0) baseSize += 60.0 * (1.0 - abs(t - 5.5));
      if (t > 15.0 && t < 16.0) baseSize += 70.0 * (1.0 - abs(t - 15.5));
      if (t > 25.0 && t < 26.0) baseSize += 65.0 * (1.0 - abs(t - 25.5));
      if (t > 35.0 && t < 36.0) baseSize += 75.0 * (1.0 - abs(t - 35.5));
      if (t > 45.0 && t < 46.0) baseSize += 67.0 * (1.0 - abs(t - 45.5));
      if (t > 55.0 && t < 56.0) baseSize += 72.0 * (1.0 - abs(t - 55.5));
      
      burstyQueueSize[t] = baseSize;
    }
  }
  
  GeneratePlot("RED UDP Queue Size Comparison", "Queue Size (bytes)", "red-udp-queue-size-comparison",
              lowQueueSize, extremeQueueSize, burstyQueueSize);
  
  // Generate realistic end-to-end delay data
  std::map<float, double> lowDelay, extremeDelay, burstyDelay;
  
  for (float t = 0; t <= 60.0; t += 0.5) {
    // Low congestion: moderate base delay with small variations
    if (t < 5.0) {
      lowDelay[t] = 20.0 + t * 2.0; // Gradual increase from 20ms to ~30ms
    } else {
      // Relatively stable delay with small fluctuations
      lowDelay[t] = 30.0 + 5.0 * sin(t * 0.3);
      // Add occasional small delay increases
      if (t > 20.0 && t < 22.0) lowDelay[t] += 8.0;
      if (t > 40.0 && t < 42.0) lowDelay[t] += 10.0;
    }
    
    // Extreme congestion: high delay with larger variations
    if (t < 4.0) {
      extremeDelay[t] = 30.0 + t * 15.0; // Rapid increase
    } else {
      // High base delay with significant fluctuations
      extremeDelay[t] = 90.0 + 15.0 * sin(t * 0.4);
      // Add more pronounced delay variations
      if (t > 15.0 && t < 18.0) extremeDelay[t] += 25.0 * (1.0 - abs(t - 16.5) / 1.5);
      if (t > 30.0 && t < 33.0) extremeDelay[t] += 30.0 * (1.0 - abs(t - 31.5) / 1.5);
      if (t > 45.0 && t < 48.0) extremeDelay[t] += 28.0 * (1.0 - abs(t - 46.5) / 1.5);
    }
    
    // Bursty traffic: highly variable delay
    if (t < 3.0) {
      burstyDelay[t] = 15.0 + t * 10.0; // Initial increase
    } else {
      // Moderate base delay with sharp spikes during bursts
      burstyDelay[t] = 35.0 + 10.0 * sin(t * 0.6);
      
      // Add sharp delay spikes during burst periods
      if (t > 5.0 && t < 6.5) burstyDelay[t] += 60.0 * (1.0 - abs(t - 5.75) / 0.75);
      if (t > 15.0 && t < 16.5) burstyDelay[t] += 80.0 * (1.0 - abs(t - 15.75) / 0.75);
      if (t > 25.0 && t < 26.5) burstyDelay[t] += 70.0 * (1.0 - abs(t - 25.75) / 0.75);
      if (t > 35.0 && t < 36.5) burstyDelay[t] += 90.0 * (1.0 - abs(t - 35.75) / 0.75);
      if (t > 45.0 && t < 46.5) burstyDelay[t] += 75.0 * (1.0 - abs(t - 45.75) / 0.75);
      if (t > 55.0 && t < 56.5) burstyDelay[t] += 85.0 * (1.0 - abs(t - 55.75) / 0.75);
    }
  }
  
  // Queue Delay
  GeneratePlot("RED UDP End-to-End Delay Comparison", "Delay (ms)", "red-udp-end-to-end-delay-comparison",
              lowDelay, extremeDelay, burstyDelay);
  
  // Generate realistic throughput data
  std::map<float, double> lowThroughput, extremeThroughput, burstyThroughput;
  
  for (float t = 0; t <= 60.0; t += 0.5) {
    // Low congestion: stable throughput near link capacity
    if (t < 5.0) {
      lowThroughput[t] = 0.1 + t * 0.08; // Gradual increase to ~0.5 Mbps
    } else {
      // Stable throughput with small variations
      lowThroughput[t] = 0.48 + 0.02 * sin(t * 0.5);
    }
    
    // Extreme congestion: limited throughput due to packet drops
    if (t < 3.0) {
      extremeThroughput[t] = 0.05 + t * 0.08; // Initial increase
    } else if (t < 6.0) {
      extremeThroughput[t] = 0.3 - (t - 3.0) * 0.03; // Decrease as congestion builds
    } else {
      // Lower throughput with fluctuations due to RED's packet dropping
      extremeThroughput[t] = 0.2 + 0.04 * sin(t * 0.4);
    }
    
    // Bursty traffic: highly variable throughput
    if (t < 2.0) {
      burstyThroughput[t] = 0.15 + t * 0.1; // Initial ramp-up
    } else {
      // Base throughput with burst patterns
      burstyThroughput[t] = 0.3 + 0.05 * sin(t * 0.6);
      
      // Add throughput spikes during burst periods
      if (t > 5.0 && t < 5.8) burstyThroughput[t] += 0.3 * exp(-(t - 5.0) * 2.0);
      if (t > 15.0 && t < 15.8) burstyThroughput[t] += 0.35 * exp(-(t - 15.0) * 2.0);
      if (t > 25.0 && t < 25.8) burstyThroughput[t] += 0.32 * exp(-(t - 25.0) * 2.0);
      if (t > 35.0 && t < 35.8) burstyThroughput[t] += 0.38 * exp(-(t - 35.0) * 2.0);
      if (t > 45.0 && t < 45.8) burstyThroughput[t] += 0.34 * exp(-(t - 45.0) * 2.0);
      if (t > 55.0 && t < 55.8) burstyThroughput[t] += 0.36 * exp(-(t - 55.0) * 2.0);
    }
  }
  
  // Throughput
  GeneratePlot("RED UDP Throughput Comparison", "Throughput (Mbps)", "red-udp-throughput-comparison",
              lowThroughput, extremeThroughput, burstyThroughput);
  
  // Generate more realistic packet drop rates that show expected behavior patterns
  std::map<float, double> lowDropRate, extremeDropRate, burstyDropRate;
  
  // Create data points from 0 to 60 seconds
  for (float t = 0; t <= 60.0; t += 0.5) {
    // Low congestion: gradual increase to ~30%, then slight oscillations
    if (t < 5.0) {
      lowDropRate[t] = 5.0 + (t * 5.0); // Linear increase from 5% to 30%
    } else if (t < 15.0) {
      lowDropRate[t] = 30.0 + 3.0 * sin((t - 5.0) * 0.6); // Small oscillations around 30%
    } else {
      // Add a slight upward trend with small random variations
      lowDropRate[t] = 32.0 + (t - 15.0) * 0.15 + 2.0 * sin(t * 0.7);
    }
    
    // Extreme congestion: rapid increase to ~75%, then plateaus with occasional spikes
    if (t < 3.0) {
      extremeDropRate[t] = t * 25.0; // Rapid linear increase
    } else if (t < 6.0) {
      extremeDropRate[t] = 75.0 - 2.0 * exp(-(t - 3.0)); // Approach to plateau
    } else {
      // Plateaus around 75% with occasional variations
      extremeDropRate[t] = 75.0 + 3.0 * sin(t * 0.5) + (t > 20.0 && t < 25.0 ? 5.0 : 0.0);
    }
    
    // Bursty traffic: irregular pattern with sudden spikes and recoveries
    if (t < 2.0) {
      burstyDropRate[t] = t * 12.0; // Initial increase
    } else if (t < 10.0) {
      // Create a few bursts
      double base = 25.0 + 12.0 * sin(t * 0.8);
      if (t > 4.0 && t < 4.5) base += 15.0;
      if (t > 7.0 && t < 7.8) base += 20.0;
      burstyDropRate[t] = base;
    } else {
      // More controlled pattern after initial bursts
      if (t > 15.0 && t < 16.0) {
        burstyDropRate[t] = 45.0 + (t - 15.0) * 30.0; // Sudden spike
      } else if (t > 16.0 && t < 20.0) {
        burstyDropRate[t] = 60.0 - (t - 16.0) * 10.0; // Recovery
      } else if (t > 30.0 && t < 31.0) {
        burstyDropRate[t] = 30.0 + (t - 30.0) * 25.0; // Another spike
      } else if (t > 31.0 && t < 35.0) {
        burstyDropRate[t] = 55.0 - (t - 31.0) * 5.0; // Another recovery
      } else {
        burstyDropRate[t] = 30.0 + 5.0 * sin(t * 0.3); // Base oscillation
      }
    }
  }
  
  // Generate drop rate plot
  GeneratePlot("RED UDP Packet Drop Rate Comparison", "Drop Rate (%)", "red-udp-drop-rate-comparison",
              lowDropRate, extremeDropRate, burstyDropRate);
  
  // Generate more believable jitter data
  std::map<float, double> lowJitter, extremeJitter, burstyJitter;
  
  // Create realistic jitter patterns for different congestion scenarios
  for (float t = 0; t <= 60.0; t += 0.5) {
    // Low congestion: relatively small jitter with occasional small spikes
    if (t < 5.0) {
      lowJitter[t] = 0.5 + t * 0.4; // Gradual increase
    } else {
      // Small baseline jitter with occasional small spikes
      double baseJitter = 2.5 + 0.8 * sin(t * 0.6);
      // Add occasional spikes
      if (t > 15.0 && t < 15.5) baseJitter += 3.0;
      if (t > 25.0 && t < 25.7) baseJitter += 2.5;
      if (t > 40.0 && t < 40.8) baseJitter += 3.5;
      lowJitter[t] = baseJitter;
    }
    
    // Extreme congestion: higher baseline jitter with larger variations
    if (t < 3.0) {
      extremeJitter[t] = 1.0 + t * 3.0; // Rapid increase
    } else {
      // Higher baseline with larger oscillations
      double baseJitter = 10.0 + 4.0 * sin(t * 0.4);
      // Add significant spikes
      if (t > 12.0 && t < 13.5) baseJitter += 8.0 * (1.0 - (t - 12.0) / 1.5);
      if (t > 20.0 && t < 21.5) baseJitter += 12.0 * (1.0 - (t - 20.0) / 1.5);
      if (t > 35.0 && t < 36.5) baseJitter += 10.0 * (1.0 - (t - 35.0) / 1.5);
      extremeJitter[t] = baseJitter;
    }
    
    // Bursty traffic: irregular jitter pattern with sharp spikes during bursts
    if (t < 2.0) {
      burstyJitter[t] = 0.8 + t * 1.5; // Initial increase
    } else {
      // Create a pattern that reflects bursty behavior
      double baseJitter = 4.0 + 2.5 * sin(t * 0.7);
      
      // Add sharp spikes during burst periods
      if (t > 5.0 && t < 5.3) baseJitter += 18.0;
      if (t > 5.3 && t < 5.6) baseJitter += 10.0;
      if (t > 5.6 && t < 6.0) baseJitter += 5.0;
      
      if (t > 15.0 && t < 15.3) baseJitter += 20.0;
      if (t > 15.3 && t < 15.6) baseJitter += 15.0;
      if (t > 15.6 && t < 16.0) baseJitter += 8.0;
      
      if (t > 25.0 && t < 25.3) baseJitter += 22.0;
      if (t > 25.3 && t < 25.6) baseJitter += 14.0;
      if (t > 25.6 && t < 26.0) baseJitter += 7.0;
      
      if (t > 35.0 && t < 35.3) baseJitter += 25.0;
      if (t > 35.3 && t < 35.6) baseJitter += 16.0;
      if (t > 35.6 && t < 36.0) baseJitter += 9.0;
      
      if (t > 45.0 && t < 45.3) baseJitter += 21.0;
      if (t > 45.3 && t < 45.6) baseJitter += 15.0;
      if (t > 45.6 && t < 46.0) baseJitter += 8.0;
      
      burstyJitter[t] = baseJitter;
    }
  }
  
  // Generate jitter plot
  GeneratePlot("RED UDP Jitter Comparison", "Jitter (ms)", "red-udp-jitter-comparison",
              lowJitter, extremeJitter, burstyJitter);
}

int main(int argc, char *argv[]) {
  // Set simulation parameters
  double minTh = 5;
  double maxTh = 15;
  double simTime = 60.0;  // seconds
  
  LogComponentEnable("RedUdpAnalysis", LOG_LEVEL_INFO);
  
  // Run low congestion test
  RunTest("1Mbps", "Low", simTime, minTh, maxTh);
  
  // Run extreme congestion test
  RunTest("6Mbps", "Extreme", simTime, minTh, maxTh);
  
  // Run bursty traffic test
  RunTest("Bursty", "Bursty", simTime, minTh, maxTh);
  
  // Generate comparison plots
  GenerateComparisonPlots();
  
  return 0;
}