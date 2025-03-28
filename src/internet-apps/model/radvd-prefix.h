/*
 * Copyright (c) 2009 Strasbourg University
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Author: Sebastien Vincent <vincent@clarinet.u-strasbg.fr>
 */

#ifndef RADVD_PREFIX_H
#define RADVD_PREFIX_H

#include "ns3/ipv6-address.h"
#include "ns3/simple-ref-count.h"

#include <stdint.h>

namespace ns3
{

/**
 * @ingroup radvd
 * @brief Router prefix for radvd application.
 */
class RadvdPrefix : public SimpleRefCount<RadvdPrefix>
{
  public:
    /**
     * @brief Constructor.
     * @param network network prefix advertised
     * @param prefixLength prefix length ( 0 < x <= 128)
     * @param preferredLifeTime preferred life time in seconds (default 7 days)
     * @param validLifeTime valid life time in seconds (default 30 days)
     * @param onLinkFlag on link flag
     * @param autonomousFlag autonomous link flag
     * @param routerAddrFlag router address flag (for Mobile IPv6)
     */
    RadvdPrefix(Ipv6Address network,
                uint8_t prefixLength,
                uint32_t preferredLifeTime = 604800,
                uint32_t validLifeTime = 2592000,
                bool onLinkFlag = true,
                bool autonomousFlag = true,
                bool routerAddrFlag = false);

    /**
     * @brief Destructor.
     */
    ~RadvdPrefix();

    /**
     * @brief Get network prefix.
     * @return network prefix
     */
    Ipv6Address GetNetwork() const;

    /**
     * @brief Set network prefix.
     * @param network network prefix
     */
    void SetNetwork(Ipv6Address network);

    /**
     * @brief Get prefix length.
     * @return prefix length
     */
    uint8_t GetPrefixLength() const;

    /**
     * @brief Set prefix length.
     * @param prefixLength prefix length
     */
    void SetPrefixLength(uint8_t prefixLength);

    /**
     * @brief Get preferred lifetime.
     * @return lifetime
     */
    uint32_t GetPreferredLifeTime() const;

    /**
     * @brief Set preferred lifetime.
     * @param preferredLifeTime lifetime
     */
    void SetPreferredLifeTime(uint32_t preferredLifeTime);

    /**
     * @brief Get valid lifetime.
     * @return lifetime
     */
    uint32_t GetValidLifeTime() const;

    /**
     * @brief Set valid lifetime.
     * @param validLifeTime lifetime
     */
    void SetValidLifeTime(uint32_t validLifeTime);

    /**
     * @brief Is on-link flag ?
     * @return true if on-link is activated, false otherwise
     */
    bool IsOnLinkFlag() const;

    /**
     * @brief Set on-link flag.
     * @param onLinkFlag value
     */
    void SetOnLinkFlag(bool onLinkFlag);

    /**
     * @brief Is autonomous flag ?
     * @return true if autonomous is activated, false otherwise
     */
    bool IsAutonomousFlag() const;

    /**
     * @brief Set autonomous flag.
     * @param autonomousFlag value
     */
    void SetAutonomousFlag(bool autonomousFlag);

    /**
     * @brief Is router address flag ?
     * @return true if router address is activated, false otherwise
     */
    bool IsRouterAddrFlag() const;

    /**
     * @brief Set router address flag.
     * @param routerAddrFlag value
     */
    void SetRouterAddrFlag(bool routerAddrFlag);

  private:
    /**
     * @brief Network prefix.
     */
    Ipv6Address m_network;

    /**
     * @brief Prefix length.
     */
    uint8_t m_prefixLength;

    /**
     * @brief Preferred time.
     */
    uint32_t m_preferredLifeTime;

    /**
     * @brief Valid time.
     */
    uint32_t m_validLifeTime;

    /**
     * @brief On link flag, indicates that this prefix can be used for on-link determination.
     */
    bool m_onLinkFlag;

    /**
     * @brief Autonomous flag, it is used for autonomous address configuration (\RFC{2462}).
     */
    bool m_autonomousFlag;

    /**
     * @brief Router address flag, indicates that router address is sent instead
     * of network prefix as is required by Mobile IPv6.
     */
    bool m_routerAddrFlag;
};

} /* namespace ns3 */

#endif /* RADVD_PREFIX_H */
