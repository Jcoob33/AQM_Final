/*
 * Copyright (c) 2007,2008 INRIA
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Author: Jahanzeb Farooq <jahanzeb.farooq@sophia.inria.fr>
 */

#ifndef SEND_PARAMS_H
#define SEND_PARAMS_H

#include <stdint.h>

namespace ns3
{

class WimaxPhy;

/**
 * @ingroup wimax
 * @brief The SendParams class defines the parameters with which Send() function of
 *  a particular PHY is called. The sole purpose of this class is to allow
 *  defining the pure virtual Send() function in the PHY base-class (WimaxPhy).
 *  This class shall be sub-classed every time a new PHY is integrated (i.e.,
 *  a new sub-class of WimaxPhy is created) which requires different or
 *  additional parameters to call its Send() function. For example as it is
 *  seen here, it has been sub-classed for the OFDM PHY layer since its Send()
 *  function requires two additional parameters.
 */
class SendParams
{
  public:
    SendParams();
    virtual ~SendParams();

  private:
};

} // namespace ns3

#endif /* SEND_PARAMS_H */

#ifndef OFDM_SEND_PARAMS_H
#define OFDM_SEND_PARAMS_H

#include "ns3/packet-burst.h"

#include <stdint.h>

namespace ns3
{

/**
 * OfdmSendParams class
 */
class OfdmSendParams : public SendParams
{
  public:
    /**
     * Constructor
     *
     * @param burst packet burst object
     * @param modulationType modulation type
     * @param direction the direction
     */
    OfdmSendParams(Ptr<PacketBurst> burst, uint8_t modulationType, uint8_t direction);
    ~OfdmSendParams() override;

    /**
     * @return the packet burst
     */
    Ptr<PacketBurst> GetBurst() const
    {
        return m_burst;
    }

    /**
     * @return the modulation type
     */
    uint8_t GetModulationType() const
    {
        return m_modulationType;
    }

    /**
     * @return the direction
     */
    uint8_t GetDirection() const
    {
        return m_direction;
    }

  private:
    Ptr<PacketBurst> m_burst; ///< packet burst
    uint8_t m_modulationType; ///< modulation type
    uint8_t m_direction;      ///< direction
};

} // namespace ns3

#endif /* OFDM_SEND_PARAMS_H */
