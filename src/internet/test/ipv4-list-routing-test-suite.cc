/*
 * Copyright (c) 2009 University of Washington
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

#include "ns3/ipv4-list-routing.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/test.h"

namespace ns3
{

/**
 * @ingroup internet-test
 *
 * @brief IPv4 dummy routing class (A)
 */
class Ipv4ARouting : public Ipv4RoutingProtocol
{
  public:
    Ptr<Ipv4Route> RouteOutput(Ptr<Packet> p,
                               const Ipv4Header& header,
                               Ptr<NetDevice> oif,
                               Socket::SocketErrno& sockerr) override
    {
        return nullptr;
    }

    bool RouteInput(Ptr<const Packet> p,
                    const Ipv4Header& header,
                    Ptr<const NetDevice> idev,
                    const UnicastForwardCallback& ucb,
                    const MulticastForwardCallback& mcb,
                    const LocalDeliverCallback& lcb,
                    const ErrorCallback& ecb) override
    {
        return false;
    }

    void NotifyInterfaceUp(uint32_t interface) override
    {
    }

    void NotifyInterfaceDown(uint32_t interface) override
    {
    }

    void NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address) override
    {
    }

    void NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address) override
    {
    }

    void SetIpv4(Ptr<Ipv4> ipv4) override
    {
    }

    void PrintRoutingTable(Ptr<OutputStreamWrapper> stream, Time::Unit unit) const override
    {
    }
};

/**
 * @ingroup internet-test
 *
 * @brief IPv4 dummy routing class (B)
 */
class Ipv4BRouting : public Ipv4RoutingProtocol
{
  public:
    Ptr<Ipv4Route> RouteOutput(Ptr<Packet> p,
                               const Ipv4Header& header,
                               Ptr<NetDevice> oif,
                               Socket::SocketErrno& sockerr) override
    {
        return nullptr;
    }

    bool RouteInput(Ptr<const Packet> p,
                    const Ipv4Header& header,
                    Ptr<const NetDevice> idev,
                    const UnicastForwardCallback& ucb,
                    const MulticastForwardCallback& mcb,
                    const LocalDeliverCallback& lcb,
                    const ErrorCallback& ecb) override
    {
        return false;
    }

    void NotifyInterfaceUp(uint32_t interface) override
    {
    }

    void NotifyInterfaceDown(uint32_t interface) override
    {
    }

    void NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address) override
    {
    }

    void NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address) override
    {
    }

    void SetIpv4(Ptr<Ipv4> ipv4) override
    {
    }

    void PrintRoutingTable(Ptr<OutputStreamWrapper> stream, Time::Unit unit) const override
    {
    }
};

/**
 * @ingroup internet-test
 *
 * @brief IPv4 ListRouting negative test.
 */
class Ipv4ListRoutingNegativeTestCase : public TestCase
{
  public:
    Ipv4ListRoutingNegativeTestCase();
    void DoRun() override;
};

Ipv4ListRoutingNegativeTestCase::Ipv4ListRoutingNegativeTestCase()
    : TestCase("Check negative priorities")
{
}

void
Ipv4ListRoutingNegativeTestCase::DoRun()
{
    Ptr<Ipv4ListRouting> lr = CreateObject<Ipv4ListRouting>();
    Ptr<Ipv4RoutingProtocol> aRouting = CreateObject<Ipv4ARouting>();
    Ptr<Ipv4RoutingProtocol> bRouting = CreateObject<Ipv4BRouting>();
    // The Ipv4BRouting should be added with higher priority (larger integer value)
    lr->AddRoutingProtocol(aRouting, -10);
    lr->AddRoutingProtocol(bRouting, -5);
    int16_t first = 3;
    uint32_t num = lr->GetNRoutingProtocols();
    NS_TEST_ASSERT_MSG_EQ(num, 2, "100");
    Ptr<Ipv4RoutingProtocol> firstRp = lr->GetRoutingProtocol(0, first);
    NS_TEST_ASSERT_MSG_EQ(-5, first, "101");
    NS_TEST_ASSERT_MSG_EQ(firstRp, bRouting, "102");
}

/**
 * @ingroup internet-test
 *
 * @brief IPv4 ListRouting positive test.
 */
class Ipv4ListRoutingPositiveTestCase : public TestCase
{
  public:
    Ipv4ListRoutingPositiveTestCase();
    void DoRun() override;
};

Ipv4ListRoutingPositiveTestCase::Ipv4ListRoutingPositiveTestCase()
    : TestCase("Check positive priorities")
{
}

void
Ipv4ListRoutingPositiveTestCase::DoRun()
{
    Ptr<Ipv4ListRouting> lr = CreateObject<Ipv4ListRouting>();
    Ptr<Ipv4RoutingProtocol> aRouting = CreateObject<Ipv4ARouting>();
    Ptr<Ipv4RoutingProtocol> bRouting = CreateObject<Ipv4BRouting>();
    // The Ipv4ARouting should be added with higher priority (larger integer
    // value) and will be fetched first below
    lr->AddRoutingProtocol(aRouting, 10);
    lr->AddRoutingProtocol(bRouting, 5);
    int16_t first = 3;
    int16_t second = 3;
    uint32_t num = lr->GetNRoutingProtocols();
    NS_TEST_ASSERT_MSG_EQ(num, 2, "200");
    Ptr<Ipv4RoutingProtocol> firstRp = lr->GetRoutingProtocol(0, first);
    NS_TEST_ASSERT_MSG_EQ(10, first, "201");
    NS_TEST_ASSERT_MSG_EQ(firstRp, aRouting, "202");
    Ptr<Ipv4RoutingProtocol> secondRp = lr->GetRoutingProtocol(1, second);
    NS_TEST_ASSERT_MSG_EQ(5, second, "203");
    NS_TEST_ASSERT_MSG_EQ(secondRp, bRouting, "204");
}

/**
 * @ingroup internet-test
 *
 * @brief IPv4 ListRouting TestSuite
 */
class Ipv4ListRoutingTestSuite : public TestSuite
{
  public:
    Ipv4ListRoutingTestSuite()
        : TestSuite("ipv4-list-routing", Type::UNIT)
    {
        AddTestCase(new Ipv4ListRoutingPositiveTestCase(), TestCase::Duration::QUICK);
        AddTestCase(new Ipv4ListRoutingNegativeTestCase(), TestCase::Duration::QUICK);
    }
};

static Ipv4ListRoutingTestSuite
    g_ipv4ListRoutingTestSuite; //!< Static variable for test initialization

} // namespace ns3
