# -*-  Mode: Python; -*-
#  Copyright (c) 2009 INESC Porto
#
#  SPDX-License-Identifier: GPL-2.0-only
#
#  Authors: Gustavo Carneiro <gjc@inescporto.pt>

from __future__ import print_function

import sys

try:
    from ns import ns
except ModuleNotFoundError:
    raise SystemExit(
        "Error: ns3 Python module not found;"
        " Python bindings may not be enabled"
        " or your PYTHONPATH might not be properly configured"
    )

DISTANCE = 20  # (m)
NUM_NODES_SIDE = 3


def main(argv):
    from ctypes import c_bool, c_char_p, c_int, create_string_buffer

    NumNodesSide = c_int(2)
    Plot = c_bool(False)
    BUFFLEN = 4096
    ResultsBuffer = create_string_buffer(b"output.xml", BUFFLEN)
    Results = c_char_p(ResultsBuffer.raw)

    cmd = ns.CommandLine(__file__)
    cmd.AddValue(
        "NumNodesSide",
        "Grid side number of nodes (total number of nodes will be this number squared)",
        NumNodesSide,
    )
    cmd.AddValue("Results", "Write XML results to file", Results, BUFFLEN)
    cmd.AddValue("Plot", "Plot the results using the matplotlib python module", Plot)
    cmd.Parse(argv)

    wifi = ns.WifiHelper()
    wifiMac = ns.WifiMacHelper()
    wifiPhy = ns.YansWifiPhyHelper()
    wifiChannel = ns.YansWifiChannelHelper.Default()
    wifiPhy.SetChannel(wifiChannel.Create())
    ssid = ns.Ssid("wifi-default")
    wifiMac.SetType("ns3::AdhocWifiMac", "Ssid", ns.SsidValue(ssid))

    internet = ns.InternetStackHelper()
    list_routing = ns.Ipv4ListRoutingHelper()
    olsr_routing = ns.OlsrHelper()
    static_routing = ns.Ipv4StaticRoutingHelper()
    list_routing.Add(static_routing, 0)
    list_routing.Add(olsr_routing, 100)
    internet.SetRoutingHelper(list_routing)

    ipv4Addresses = ns.Ipv4AddressHelper()
    ipv4Addresses.SetBase(ns.Ipv4Address("10.0.0.0"), ns.Ipv4Mask("255.255.255.0"))

    port = 9  # Discard port(RFC 863)
    inetAddress = ns.InetSocketAddress(ns.Ipv4Address("10.0.0.1"), port)
    onOffHelper = ns.OnOffHelper("ns3::UdpSocketFactory", inetAddress.ConvertTo())
    onOffHelper.SetAttribute("DataRate", ns.DataRateValue(ns.DataRate("100kbps")))
    onOffHelper.SetAttribute("OnTime", ns.StringValue("ns3::ConstantRandomVariable[Constant=1]"))
    onOffHelper.SetAttribute("OffTime", ns.StringValue("ns3::ConstantRandomVariable[Constant=0]"))

    addresses = []
    nodes = []

    if NumNodesSide.value == 2:
        num_nodes_side = NUM_NODES_SIDE
    else:
        num_nodes_side = NumNodesSide.value

    nodes = ns.NodeContainer(num_nodes_side * num_nodes_side)
    accumulator = 0
    for xi in range(num_nodes_side):
        for yi in range(num_nodes_side):
            node = nodes.Get(accumulator)
            accumulator += 1
            container = ns.NodeContainer(node)
            internet.Install(container)

            mobility = ns.CreateObject[ns.ConstantPositionMobilityModel]()
            mobility.SetPosition(ns.Vector(xi * DISTANCE, yi * DISTANCE, 0))
            node.AggregateObject(mobility)

            device = wifi.Install(wifiPhy, wifiMac, node)
            ipv4_interfaces = ipv4Addresses.Assign(device)
            addresses.append(ipv4_interfaces.GetAddress(0))

    for i, node in [(i, nodes.Get(i)) for i in range(nodes.GetN())]:
        destaddr = addresses[(len(addresses) - 1 - i) % len(addresses)]
        # print (i, destaddr)
        onOffHelper.SetAttribute(
            "Remote",
            ns.AddressValue(ns.InetSocketAddress(destaddr, port).ConvertTo()),
        )
        container = ns.NodeContainer(node)
        app = onOffHelper.Install(container)
        urv = ns.CreateObject[ns.UniformRandomVariable]()  # ns.cppyy.gbl.get_rng()
        startDelay = ns.Seconds(urv.GetValue(20, 30))
        app.Start(startDelay)

    # internet.EnablePcapAll("wifi-olsr")
    flowmon_helper = ns.FlowMonitorHelper()
    # flowmon_helper.SetMonitorAttribute("StartTime", ns.TimeValue(ns.Seconds(31)))
    monitor = flowmon_helper.InstallAll()
    monitor = flowmon_helper.GetMonitor()
    monitor.SetAttribute("DelayBinWidth", ns.DoubleValue(0.001))
    monitor.SetAttribute("JitterBinWidth", ns.DoubleValue(0.001))
    monitor.SetAttribute("PacketSizeBinWidth", ns.DoubleValue(20))

    ns.Simulator.Stop(ns.Seconds(44))
    ns.Simulator.Run()

    def print_stats(os, st):
        print("  Tx Bytes: ", st.txBytes, file=os)
        print("  Rx Bytes: ", st.rxBytes, file=os)
        print("  Tx Packets: ", st.txPackets, file=os)
        print("  Rx Packets: ", st.rxPackets, file=os)
        print("  Lost Packets: ", st.lostPackets, file=os)
        if st.rxPackets > 0:
            print("  Mean{Delay}: ", (st.delaySum.GetSeconds() / st.rxPackets), file=os)
            print("  Mean{Jitter}: ", (st.jitterSum.GetSeconds() / (st.rxPackets - 1)), file=os)
            print("  Mean{Hop Count}: ", float(st.timesForwarded) / st.rxPackets + 1, file=os)

        if 0:
            print("Delay Histogram", file=os)
            for i in range(st.delayHistogram.GetNBins()):
                print(
                    " ",
                    i,
                    "(",
                    st.delayHistogram.GetBinStart(i),
                    "-",
                    st.delayHistogram.GetBinEnd(i),
                    "): ",
                    st.delayHistogram.GetBinCount(i),
                    file=os,
                )
            print("Jitter Histogram", file=os)
            for i in range(st.jitterHistogram.GetNBins()):
                print(
                    " ",
                    i,
                    "(",
                    st.jitterHistogram.GetBinStart(i),
                    "-",
                    st.jitterHistogram.GetBinEnd(i),
                    "): ",
                    st.jitterHistogram.GetBinCount(i),
                    file=os,
                )
            print("PacketSize Histogram", file=os)
            for i in range(st.packetSizeHistogram.GetNBins()):
                print(
                    " ",
                    i,
                    "(",
                    st.packetSizeHistogram.GetBinStart(i),
                    "-",
                    st.packetSizeHistogram.GetBinEnd(i),
                    "): ",
                    st.packetSizeHistogram.GetBinCount(i),
                    file=os,
                )

        for reason, drops in enumerate(st.packetsDropped):
            print("  Packets dropped by reason %i: %i" % (reason, drops), file=os)
        # for reason, drops in enumerate(st.bytesDropped):
        #    print "Bytes dropped by reason %i: %i" % (reason, drops)

    monitor.CheckForLostPackets()
    classifier = flowmon_helper.GetClassifier()

    if Results.value != b"output.xml":
        for flow_id, flow_stats in monitor.GetFlowStats():
            t = classifier.FindFlow(flow_id)
            proto = {6: "TCP", 17: "UDP"}[t.protocol]
            print(
                "FlowID: %i (%s %s/%s --> %s/%i)"
                % (
                    flow_id,
                    proto,
                    t.sourceAddress,
                    t.sourcePort,
                    t.destinationAddress,
                    t.destinationPort,
                )
            )
            print_stats(sys.stdout, flow_stats)
    else:
        res = monitor.SerializeToXmlFile(Results.value.decode("utf-8"), True, True)
        print(res)

    if Plot.value:
        from matplotlib import pyplot as plt

        delays = []
        for flow_id, flow_stats in monitor.GetFlowStats():
            tupl = classifier.FindFlow(flow_id)
            if tupl.protocol == 17 and tupl.sourcePort == 698:
                continue
            delays.append(flow_stats.delaySum.GetSeconds() / flow_stats.rxPackets)
        plt.hist(delays, 20)
        plt.xlabel("Delay (s)")
        plt.ylabel("Number of Flows")
        plt.show()

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
