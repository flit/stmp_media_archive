///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Freescale Semiconductor, Inc. All rights reserved.
// 
// Freescale Semiconductor, Inc.
// Proprietary & Confidential
// 
// This source code and the algorithms implemented therein constitute
// confidential information and may comprise trade secrets of Freescale Semiconductor, Inc.
// or its associates, and any use thereof is subject to the terms and
// conditions of the Confidential Disclosure Agreement pursual to which this
// source code was originally received.
///////////////////////////////////////////////////////////////////////////////
//! \file Metadata.cpp
//! \brief Class to wrap a metadata buffer.
///////////////////////////////////////////////////////////////////////////////

#include "Metadata.h"
#include <string.h>

using namespace nand;

///////////////////////////////////////////////////////////////////////////////
// Source
///////////////////////////////////////////////////////////////////////////////

uint32_t Metadata::getLba() const
{
    assert(m_fields);
    return m_fields->lba0 | (m_fields->lba1 << 16);
}

uint16_t Metadata::getLsi() const
{
    assert(m_fields);
    return m_fields->lsi;
}

uint8_t Metadata::getBlockNumber() const
{
    assert(m_fields);
    return m_fields->blockNumber;
}

uint32_t Metadata::getSignature() const
{
    assert(m_fields);
    return ((m_fields->tag0 << 24)
            | (m_fields->tag1 << 16)
            | (m_fields->tag2 << 8)
            | (m_fields->tag3));
}

//! Metadata flags are set when the bit is 0.
//!
bool Metadata::isFlagSet(uint8_t flagMask) const
{
    assert(m_fields);
    return ((m_fields->flags & flagMask) == 0);
}

bool Metadata::isMarkedBad() const
{
    assert(m_fields);
    return m_fields->blockStatus != 0xff;
}

bool Metadata::isErased() const
{
    assert(m_fields);
    return ((uint32_t *)m_fields)[0] == 0xffffffff
        && ((uint32_t *)m_fields)[1] == 0xffffffff
        && m_fields->flags == 0xff
        && m_fields->reserved == 0xff;
}

void Metadata::setLba(uint32_t lba)
{
    assert(m_fields);
    m_fields->lba0 = lba & 0xffff;
    m_fields->lba1 = (lba >> 16) & 0xffff;
}

void Metadata::setLsi(uint16_t lsi)
{
    assert(m_fields);
    m_fields->lsi = lsi;
}
    
void Metadata::setBlockNumber(uint8_t blockNumber)
{
    assert(m_fields);
    m_fields->blockNumber = blockNumber;
}

void Metadata::setSignature(uint32_t signature)
{
    assert(m_fields);
    m_fields->tag3 = (signature & 0xff);
    m_fields->tag2 = (signature >> 8) & 0xff;
    m_fields->tag1 = (signature >> 16) & 0xff;
    m_fields->tag0 = (signature >> 24) & 0xff;
}

//! Set flag by setting the bit to 0.
//!
void Metadata::setFlag(uint8_t flagMask)
{
    assert(m_fields);
    m_fields->flags &= ~flagMask;
}

//! Clear flag by setting its bit to 1.
//!
void Metadata::clearFlag(uint8_t flagMask)
{
    assert(m_fields);
    m_fields->flags |= flagMask;
}

void Metadata::markBad()
{
    assert(m_fields);
    m_fields->blockStatus = 0;
}

void Metadata::erase()
{
    assert(m_fields);
    memset(m_fields, 0xff, sizeof(*m_fields));
}

void Metadata::prepare(uint32_t lba, uint32_t lsi)
{
    erase();
    setLba(lba);
    setLsi(lsi);
}

void Metadata::prepare(uint32_t signature)
{
    erase();
    setSignature(signature);
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
