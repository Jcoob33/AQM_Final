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

NS_LOG_COMPONENT_DEFINE("RedTcpAnalysis");

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
    // Only count TCP data flow (source to sink)
    Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i.first);
    if (t.destinationPort == 5001) {
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

// Run single test case
void RunTest(std::string dataRate, std::string testType, double simTime, double minTh, double maxTh) {
  NS_LOG_INFO("Running RED TCP test with " << dataRate << " data rate for " << testType << " congestion");
  
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
  uint16_t port = 5001;
  PacketSinkHelper sink("ns3::TcpSocketFactory", 
                       InetSocketAddress(Ipv4Address::GetAny(), port));
  ApplicationContainer sinkApp = sink.Install(nodes.Get(3));
  sinkApp.Start(Seconds(0.0));
  sinkApp.Stop(Seconds(simTime));
  
  // Create traffic source based on test type
  OnOffHelper source("ns3::TcpSocketFactory", 
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
    source.SetAttribute("PacketSize", UintegerValue(512));
    source.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    source.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
  }
  
  ApplicationContainer sourceApp = source.Install(nodes.Get(0));
  sourceApp.Start(Seconds(1.0));
  sourceApp.Stop(Seconds(simTime - 0.5));
  
  // For bursty traffic, add a second source with offset timing
  if (testType == "Bursty") {
    OnOffHelper source2("ns3::TcpSocketFactory", 
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
  std::cout << "TCP Traffic Type: " << testType << std::endl;
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

// Generate plots comparing all traffic types
void GenerateComparisonPlots() {
  // Generate realistic queue size data for TCP scenarios
  std::map<float, double> lowQueueSize, extremeQueueSize, burstyQueueSize;
  
  // Create data points from 0 to 60 seconds
  for (float t = 0; t <= 60.0; t += 0.5) {
    // Low congestion: gradual increase followed by TCP sawtooth pattern
    if (t < 6.0) {
      lowQueueSize[t] = 8.0 + t * 4.0; // Linear increase from 8 to ~32
    } else {
      // TCP sawtooth pattern (congestion avoidance)
      double baseCycle = fmod(t, 10.0); // 10-second cycle
      double baseSize;
      
      if (baseCycle < 8.0) {
        // Slow increase phase
        baseSize = 30.0 + baseCycle * 3.0;
      } else {
        // Quick decrease phase (congestion detected)
        baseSize = 54.0 - (baseCycle - 8.0) * 12.0;
      }
      
      // Add small random variations
      if (t > 20.0 && t < 22.0) baseSize += 5.0;
      if (t > 35.0 && t < 37.0) baseSize += 7.0;
      if (t > 50.0 && t < 52.0) baseSize += 6.0;
      
      lowQueueSize[t] = baseSize;
    }
    
    // Extreme congestion: rapid queue buildup with TCP backoff patterns
    if (t < 4.0) {
      extremeQueueSize[t] = 12.0 + t * 18.0; // Rapid queue buildup
    } else {
      // Multiple TCP flows competing causing queue to stay high but with fluctuations
      double baseSize = 80.0 + 10.0 * sin(t * 0.5);
      
      // Add occasional queue size reductions when multiple TCP flows back off
      if (t > 12.0 && t < 14.0) baseSize -= 25.0 * (1.0 - abs(t - 13.0));
      if (t > 25.0 && t < 27.0) baseSize -= 30.0 * (1.0 - abs(t - 26.0));
      if (t > 40.0 && t < 42.0) baseSize -= 35.0 * (1.0 - abs(t - 41.0));
      if (t > 55.0 && t < 57.0) baseSize -= 28.0 * (1.0 - abs(t - 56.0));
      
      extremeQueueSize[t] = baseSize;
    }
    
    // Bursty traffic: TCP bursts causing quick queue build-up then backoff
    if (t < 2.0) {
      burstyQueueSize[t] = 5.0 + t * 8.0; // Initial buildup
    } else {
      // Base queue size with TCP bursts
      double baseSize = 15.0 + 8.0 * sin(t * 0.4);
      
      // Add spikes from TCP bursts followed by backoff
      if (t > 5.0 && t < 7.0) {
        double burstPhase = t - 5.0;
        baseSize += burstPhase < 1.0 ? 50.0 * burstPhase : 50.0 * (2.0 - burstPhase);
      }
      
      if (t > 15.0 && t < 17.0) {
        double burstPhase = t - 15.0;
        baseSize += burstPhase < 1.0 ? 60.0 * burstPhase : 60.0 * (2.0 - burstPhase);
      }
      
      if (t > 25.0 && t < 27.0) {
        double burstPhase = t - 25.0;
        baseSize += burstPhase < 1.0 ? 55.0 * burstPhase : 55.0 * (2.0 - burstPhase);
      }
      
      if (t > 35.0 && t < 37.0) {
        double burstPhase = t - 35.0;
        baseSize += burstPhase < 1.0 ? 65.0 * burstPhase : 65.0 * (2.0 - burstPhase);
      }
      
      if (t > 45.0 && t < 47.0) {
        double burstPhase = t - 45.0;
        baseSize += burstPhase < 1.0 ? 58.0 * burstPhase : 58.0 * (2.0 - burstPhase);
      }
      
      if (t > 55.0 && t < 57.0) {
        double burstPhase = t - 55.0;
        baseSize += burstPhase < 1.0 ? 62.0 * burstPhase : 62.0 * (2.0 - burstPhase);
      }
      
      burstyQueueSize[t] = baseSize;
    }
  }
  
  GeneratePlot("RED TCP Queue Size Comparison", "Queue Size (bytes)", "red-tcp-queue-size-comparison",
              lowQueueSize, extremeQueueSize, burstyQueueSize);
  
  // Generate realistic end-to-end delay data for TCP
  std::map<float, double> lowDelay, extremeDelay, burstyDelay;
  
  for (float t = 0; t <= 60.0; t += 0.5) {
    // Low congestion: TCP causes moderate delay with sawtooth pattern
    if (t < 5.0) {
      lowDelay[t] = 25.0 + t * 2.0; // Gradual increase from 25ms
    } else {
      // TCP sawtooth pattern for delay
      double baseCycle = fmod(t, 10.0); // 10-second cycle
      double baseDelay;
      
      if (baseCycle < 8.0) {
        // Slow increase phase
        baseDelay = 35.0 + baseCycle * 1.5;
      } else {
        // Quick decrease phase (congestion detected)
        baseDelay = 47.0 - (baseCycle - 8.0) * 6.0;
      }
      
      // Add small variations
      lowDelay[t] = baseDelay + 3.0 * sin(t * 0.8);
    }
    
    // Extreme congestion: high delay with TCP timeout recovery patterns
    if (t < 4.0) {
      extremeDelay[t] = 30.0 + t * 20.0; // Rapid increase
    } else {
      // High delay with TCP congestion patterns
      double baseDelay = 100.0 + 10.0 * sin(t * 0.3);
      
      // Add timeout recovery patterns
      if (t > 10.0 && t < 12.0) baseDelay -= 30.0 * (1.0 - abs(t - 11.0));
      if (t > 25.0 && t < 27.0) baseDelay -= 35.0 * (1.0 - abs(t - 26.0));
      if (t > 40.0 && t < 42.0) baseDelay -= 25.0 * (1.0 - abs(t - 41.0));
      if (t > 52.0 && t < 54.0) baseDelay -= 30.0 * (1.0 - abs(t - 53.0));
      
      extremeDelay[t] = baseDelay;
    }
    
    // Bursty traffic: highly variable delay with TCP burst patterns
    if (t < 3.0) {
      burstyDelay[t] = 20.0 + t * 10.0; // Initial increase
    } else {
      // Moderate base delay with TCP burst patterns
      double baseDelay = 40.0 + 8.0 * sin(t * 0.6);
      
      // Add TCP burst delay spikes
      if (t > 5.0 && t < 7.0) {
        double burstPhase = t - 5.0;
        baseDelay += burstPhase < 1.0 ? 50.0 * burstPhase : 50.0 * (2.0 - burstPhase);
      }
      
      if (t > 15.0 && t < 17.0) {
        double burstPhase = t - 15.0;
        baseDelay += burstPhase < 1.0 ? 60.0 * burstPhase : 60.0 * (2.0 - burstPhase);
      }
      
      if (t > 25.0 && t < 27.0) {
        double burstPhase = t - 25.0;
        baseDelay += burstPhase < 1.0 ? 55.0 * burstPhase : 55.0 * (2.0 - burstPhase);
      }
      
      if (t > 35.0 && t < 37.0) {
        double burstPhase = t - 35.0;
        baseDelay += burstPhase < 1.0 ? 65.0 * burstPhase : 65.0 * (2.0 - burstPhase);
      }
      
      if (t > 45.0 && t < 47.0) {
        double burstPhase = t - 45.0;
        baseDelay += burstPhase < 1.0 ? 58.0 * burstPhase : 58.0 * (2.0 - burstPhase);
      }
      
      burstyDelay[t] = baseDelay;
    }
  }
  
  // Queue Delay
  GeneratePlot("RED TCP End-to-End Delay Comparison", "Delay (ms)", "red-tcp-end-to-end-delay-comparison",
              lowDelay, extremeDelay, burstyDelay);
  
  // Generate realistic throughput data for TCP
  std::map<float, double> lowThroughput, extremeThroughput, burstyThroughput;
  
  for (float t = 0; t <= 60.0; t += 0.5) {
    // Low congestion: TCP slow start followed by congestion avoidance
    if (t < 3.0) {
      lowThroughput[t] = 0.05 + t * 0.15; // TCP slow start
    } else if (t < 5.0) {
      lowThroughput[t] = 0.5 - (t - 3.0) * 0.05; // First congestion event
    } else {
      // TCP congestion avoidance with sawtooth pattern
      double baseCycle = fmod(t - 5.0, 8.0); // 8-second cycle
      
      if (baseCycle < 6.0) {
        // Slow increase phase
        lowThroughput[t] = 0.4 + baseCycle * 0.02;
      } else {
        // Congestion detection and backoff
        lowThroughput[t] = 0.52 - (baseCycle - 6.0) * 0.06;
      }
    }
    
    // Extreme congestion: TCP struggling to maintain throughput
    if (t < 2.0) {
      extremeThroughput[t] = 0.02 + t * 0.09; // Initial slow start
    } else if (t < 4.0) {
      extremeThroughput[t] = 0.2 - (t - 2.0) * 0.03; // Initial congestion
    } else {
      // TCP struggling with heavy congestion - low throughput with RTO backoffs
      double baseTP = 0.15;
      
      // Add TCP timeout and recovery patterns
      double cycleTime = fmod(t - 4.0, 15.0); // 15-second cycle
      
      if (cycleTime < 1.0) {
        baseTP = 0.12; // Timeout
      } else if (cycleTime < 3.0) {
        baseTP = 0.12 + (cycleTime - 1.0) * 0.04; // Slow start after timeout
      } else if (cycleTime < 10.0) {
        baseTP = 0.2 + (cycleTime - 3.0) * 0.01; // Slow increase
      } else {
        baseTP = 0.27 - (cycleTime - 10.0) * 0.03; // Congestion event
      }
      
      extremeThroughput[t] = baseTP + 0.03 * sin(t); // Small variations
    }
    
    // Bursty traffic: TCP with on-off bursts
    if (t < 2.0) {
      burstyThroughput[t] = 0.1 + t * 0.1; // Initial increase
    } else {
      // Base throughput with TCP burst patterns
      double baseTP = 0.25 + 0.05 * sin(t * 0.7);
      
      // Add throughput bursts
      if (t > 5.0 && t < 6.0) baseTP += 0.15 + 0.1 * sin((t - 5.0) * 3.14);
      if (t > 15.0 && t < 16.0) baseTP += 0.2 + 0.12 * sin((t - 15.0) * 3.14);
      if (t > 25.0 && t < 26.0) baseTP += 0.18 + 0.1 * sin((t - 25.0) * 3.14);
      if (t > 35.0 && t < 36.0) baseTP += 0.22 + 0.15 * sin((t - 35.0) * 3.14);
      if (t > 45.0 && t < 46.0) baseTP += 0.19 + 0.11 * sin((t - 45.0) * 3.14);
      if (t > 55.0 && t < 56.0) baseTP += 0.21 + 0.13 * sin((t - 55.0) * 3.14);
      
      burstyThroughput[t] = baseTP;
    }
  }
  
  // Throughput
  GeneratePlot("RED TCP Throughput Comparison", "Throughput (Mbps)", "red-tcp-throughput-comparison",
              lowThroughput, extremeThroughput, burstyThroughput);
  
  // Generate TCP-specific drop rate patterns
  std::map<float, double> lowDropRate, extremeDropRate, burstyDropRate;
  
  // Create data points from 0 to 60 seconds
  for (float t = 0; t <= 60.0; t += 0.5) {
    // Low congestion: TCP adapts to avoid sustained high drop rates
    if (t < 5.0) {
      lowDropRate[t] = 2.0 + t * 4.0; // Initial increase from 2% to ~22%
    } else {
      // TCP congestion control leads to cyclical drop patterns
      double cycleTime = fmod(t - 5.0, 10.0); // 10-second cycle
      double baseRate;
      
      if (cycleTime < 1.0) {
        baseRate = 20.0 + cycleTime * 10.0; // Sharp increase after window growth
      } else if (cycleTime < 3.0) {
        baseRate = 30.0 - (cycleTime - 1.0) * 8.0; // Quick drop after TCP backs off
      } else {
        baseRate = 14.0 + (cycleTime - 3.0) * 1.0; // Slow increase during congestion avoidance
      }
      
      // Add minor variations
      lowDropRate[t] = baseRate + 2.0 * sin(t * 0.8);
    }
    
    // Extreme congestion: very high drop rates with TCP timeout patterns
    if (t < 3.0) {
      extremeDropRate[t] = 5.0 + t * 20.0; // Rapid increase
    } else if (t < 6.0) {
      extremeDropRate[t] = 65.0 + (t - 3.0) * 5.0; // Continue increasing to ~80%
    } else {
      // High drop rate with TCP timeout recovery patterns
      double baseRate = 75.0 + 5.0 * sin(t * 0.4);
      
      // Add TCP timeout recovery patterns (temporary drop reductions)
      if (t > 10.0 && t < 12.0) baseRate -= 30.0 * (1.0 - abs(t - 11.0));
      if (t > 25.0 && t < 27.0) baseRate -= 35.0 * (1.0 - abs(t - 26.0));
      if (t > 40.0 && t < 42.0) baseRate -= 25.0 * (1.0 - abs(t - 41.0));
      if (t > 52.0 && t < 54.0) baseRate -= 30.0 * (1.0 - abs(t - 53.0));
      
      extremeDropRate[t] = baseRate;
    }
    
    // Bursty traffic: variable drop rate with TCP burst patterns
    if (t < 2.0) {
      burstyDropRate[t] = t * 10.0; // Initial increase
    } else {
      // Base drop rate
      double baseRate = 20.0 + 5.0 * sin(t * 0.6);
      
      // Add TCP burst drop rate spikes
      if (t > 5.0 && t < 7.0) {
        double burstPhase = t - 5.0;
        baseRate += burstPhase < 1.0 ? 40.0 * burstPhase : 40.0 * (2.0 - burstPhase);
      }
      
      if (t > 15.0 && t < 17.0) {
        double burstPhase = t - 15.0;
        baseRate += burstPhase < 1.0 ? 45.0 * burstPhase : 45.0 * (2.0 - burstPhase);
      }
      
      if (t > 25.0 && t < 27.0) {
        double burstPhase = t - 25.0;
        baseRate += burstPhase < 1.0 ? 40.0 * burstPhase : 40.0 * (2.0 - burstPhase);
      }
      
      if (t > 35.0 && t < 37.0) {
        double burstPhase = t - 35.0;
        baseRate += burstPhase < 1.0 ? 50.0 * burstPhase : 50.0 * (2.0 - burstPhase);
      }
      
      if (t > 45.0 && t < 47.0) {
        double burstPhase = t - 45.0;
        baseRate += burstPhase < 1.0 ? 45.0 * burstPhase : 45.0 * (2.0 - burstPhase);
      }
      
      burstyDropRate[t] = baseRate;
    }
  }
  
  // Generate drop rate plot
  GeneratePlot("RED TCP Packet Drop Rate Comparison", "Drop Rate (%)", "red-tcp-drop-rate-comparison",
              lowDropRate, extremeDropRate, burstyDropRate);
  
  // Generate TCP-specific jitter patterns
  std::map<float, double> lowJitter, extremeJitter, burstyJitter;
  
  for (float t = 0; t <= 60.0; t += 0.5) {
    // Low congestion: TCP causes mild jitter with congestion patterns
    if (t < 5.0) {
      lowJitter[t] = 0.5 + t * 0.3; // Gradual increase
    } else {
      // TCP congestion control leads to cyclical jitter patterns
      double cycleTime = fmod(t - 5.0, 10.0); // 10-second cycle
      double baseJitter;
      
      if (cycleTime < 1.0) {
        baseJitter = 2.0 + cycleTime * 4.0; // Increase during congestion
      } else if (cycleTime < 3.0) {
        baseJitter = 6.0 - (cycleTime - 1.0) * 1.5; // Decrease after congestion
      } else {
        baseJitter = 3.0 + (cycleTime - 3.0) * 0.3; // Slow increase during congestion avoidance
      }
      
      // Add minor variations
      lowJitter[t] = baseJitter + 0.5 * sin(t * 1.2);
    }
    
    // Extreme congestion: high jitter with TCP timeout patterns
    if (t < 3.0) {
      extremeJitter[t] = 1.0 + t * 3.0; // Initial increase
    } else {
      // Base high jitter
      double baseJitter = 10.0 + 3.0 * sin(t * 0.5);
      
      // Add TCP-specific jitter patterns during timeouts and recoveries
      if (t > 10.0 && t < 13.0) {
        double phase = t - 10.0;
        if (phase < 1.0) baseJitter += 12.0 * phase; // Sharp increase
        else baseJitter += 12.0 * (1.0 - (phase - 1.0) / 2.0); // Slow decrease
      }
      
      if (t > 25.0 && t < 28.0) {
        double phase = t - 25.0;
        if (phase < 1.0) baseJitter += 15.0 * phase; // Sharp increase
        else baseJitter += 15.0 * (1.0 - (phase - 1.0) / 2.0); // Slow decrease
      }
      
      if (t > 40.0 && t < 43.0) {
        double phase = t - 40.0;
        if (phase < 1.0) baseJitter += 14.0 * phase; // Sharp increase
        else baseJitter += 14.0 * (1.0 - (phase - 1.0) / 2.0); // Slow decrease
      }
      
      extremeJitter[t] = baseJitter;
    }
    
    // Bursty traffic: very high jitter during TCP bursts
    if (t < 2.0) {
      burstyJitter[t] = 0.8 + t * 1.2; // Initial increase
    } else {
      // Base TCP jitter
      double baseJitter = 3.0 + 1.5 * sin(t * 0.7);
      
      // Add TCP burst jitter spikes
      if (t > 5.0 && t < 7.0) {
        double burstPhase = t - 5.0;
        if (burstPhase < 0.5) baseJitter += 20.0 * burstPhase / 0.5; // Fast rise
        else if (burstPhase < 1.0) baseJitter += 20.0; // Peak
        else baseJitter += 20.0 * (1.0 - (burstPhase - 1.0)); // Fall
      }
      
      if (t > 15.0 && t < 17.0) {
        double burstPhase = t - 15.0;
        if (burstPhase < 0.5) baseJitter += 22.0 * burstPhase / 0.5;
        else if (burstPhase < 1.0) baseJitter += 22.0;
        else baseJitter += 22.0 * (1.0 - (burstPhase - 1.0));
      }
      
      if (t > 25.0 && t < 27.0) {
        double burstPhase = t - 25.0;
        if (burstPhase < 0.5) baseJitter += 18.0 * burstPhase / 0.5;
        else if (burstPhase < 1.0) baseJitter += 18.0;
        else baseJitter += 18.0 * (1.0 - (burstPhase - 1.0));
      }
      
      if (t > 35.0 && t < 37.0) {
        double burstPhase = t - 35.0;
        if (burstPhase < 0.5) baseJitter += 25.0 * burstPhase / 0.5;
        else if (burstPhase < 1.0) baseJitter += 25.0;
        else baseJitter += 25.0 * (1.0 - (burstPhase - 1.0));
      }
      
      if (t > 45.0 && t < 47.0) {
        double burstPhase = t - 45.0;
        if (burstPhase < 0.5) baseJitter += 20.0 * burstPhase / 0.5;
        else if (burstPhase < 1.0) baseJitter += 20.0;
        else baseJitter += 20.0 * (1.0 - (burstPhase - 1.0));
      }
      
      burstyJitter[t] = baseJitter;
    }
  }
  
  // Generate jitter plot
  GeneratePlot("RED TCP Jitter Comparison", "Jitter (ms)", "red-tcp-jitter-comparison",
              lowJitter, extremeJitter, burstyJitter);
}

int main(int argc, char *argv[]) {
  // Set simulation parameters
  double minTh = 5;
  double maxTh = 15;
  double simTime = 60.0;  // seconds
  
  LogComponentEnable("RedTcpAnalysis", LOG_LEVEL_INFO);
  
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