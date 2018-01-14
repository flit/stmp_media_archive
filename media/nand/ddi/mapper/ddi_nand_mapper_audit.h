////////////////////////////////////////////////////////////////////////////////
//! \addtogroup ddi_nand_mapper
//! @{
//!
//  Copyright (c) 2006-2007 SigmaTel, Inc.
//!
//! \file    ddi_nand_mapper_audit.h
//! \brief   NAND mapper audit functions.
//!
//! Declarations for the mapper audit functions.
//!
////////////////////////////////////////////////////////////////////////////////

#if !defined(_ddi_nand_mapper_audit_h_)
#define _ddi_nand_mapper_audit_h_

#include "types.h"
#include "errordefs.h"

//! \brief This function runs Audit functions.
RtStatus_t ddi_nand_mapper_DoAudits(void);

#endif // _ddi_nand_mapper_audit_h_

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
