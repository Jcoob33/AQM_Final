/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Adaptation of examples/udp/udp-client-server.cc for
// Click based nodes.
//
// Network topology
//
//         172.16.1.0/24
//      (1.1) (1.2)  (1.3)
//        n0    n1    n2
//        |     |     |
//        =============
//             LAN
//
// - UDP flows from n0 to n1 and n2 to n1
// - All nodes are Click based.
// - The single ethernet interface that each node
//   uses is named 'eth0' in the Click file.
//

#include "ns3/applications-module.h"
#include "ns3/click-internet-stack-helper.h"
#include "ns3/core-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-click-routing.h"
#include "ns3/network-module.h"

#include <fstream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("NsclickUdpClientServerCsma");

int
main(int argc, char* argv[])
{
    std::string clickConfigFolder = "src/click/examples";

    CommandLine cmd(__FILE__);
    cmd.AddValue("clickConfigFolder",
                 "Base folder for click configuration files",
                 clickConfigFolder);
    cmd.Parse(argc, argv);

    //
    // Enable logging for UdpClient and
    //
    LogComponentEnable("NsclickUdpClientServerCsma", LOG_LEVEL_INFO);

    //
    // Explicitly create the nodes required by the topology (shown above).
    //
    NS_LOG_INFO("Create nodes.");
    NodeContainer n;
    n.Create(3);

    NS_LOG_INFO("Create channels.");
    //
    // Explicitly create the channels required by the topology (shown above).
    //
    CsmaHelper csma;
    csma.SetChannelAttribute("DataRate", DataRateValue(DataRate(5000000)));
    csma.SetChannelAttribute("Delay", TimeValue(MilliSeconds(2)));
    csma.SetDeviceAttribute("Mtu", UintegerValue(1400));
    NetDeviceContainer d = csma.Install(n);

    //
    // Install Click on the nodes
    //
    ClickInternetStackHelper clickinternet;
    clickinternet.SetClickFile(n, clickConfigFolder + "/nsclick-lan-single-interface.click");
    clickinternet.SetRoutingTableElement(n, "rt");
    clickinternet.Install(n);

    Ipv4AddressHelper ipv4;
    //
    // We've got the "hardware" in place.  Now we need to add IP addresses.
    //
    NS_LOG_INFO("Assign IP Addresses.");
    ipv4.SetBase("172.16.1.0", "255.255.255.0");
    Ipv4InterfaceContainer i = ipv4.Assign(d);

    NS_LOG_INFO("Create Applications.");
    //
    // Create one udpServer applications on node one.
    //
    uint16_t port = 4000;
    UdpServerHelper server(port);
    ApplicationContainer apps = server.Install(n.Get(1));
    apps.Start(Seconds(1));
    apps.Stop(Seconds(10));

    //
    // Create one UdpClient application to send UDP datagrams from node zero to
    // node one.
    //
    uint32_t MaxPacketSize = 1024;
    Time interPacketInterval = Seconds(0.05);
    uint32_t maxPacketCount = 320;
    UdpClientHelper client(i.GetAddress(1), port);
    client.SetAttribute("MaxPackets", UintegerValue(maxPacketCount));
    client.SetAttribute("Interval", TimeValue(interPacketInterval));
    client.SetAttribute("PacketSize", UintegerValue(MaxPacketSize));
    apps = client.Install(NodeContainer(n.Get(0), n.Get(2)));
    apps.Start(Seconds(2));
    apps.Stop(Seconds(10));

    csma.EnablePcap("nsclick-udp-client-server-csma", d, false);

    //
    // Now, do the actual simulation.
    //
    NS_LOG_INFO("Run Simulation.");
    Simulator::Stop(Seconds(20));
    Simulator::Run();
    Simulator::Destroy();
    NS_LOG_INFO("Done.");

    return 0;
}
