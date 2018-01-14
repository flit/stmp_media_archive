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
#include "drivers/media/common/media_unit_test_helpers.h"
#include "drivers/ssp/mmcsd/ddi_ssp_mmcsd_board.h"
#include "drivers/media/cache/media_cache.h"

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

//! Set to 1 to use a relatively small set of prepared sector pattern buffers, versus
//! filling the write/compare buffer with a unique pattern for each sector. Enabling this
//! improves the sectors per second performance but more sectors have exactly the same
//! pattern and thus cannot be distinguished. The number of unique patterns is set with
//! the #kPatternBufferCount constant.
#define USE_LIMITED_SECTOR_PATTERNS 1

//! Set this macro to 1 to run a simple test of writing and reading back one sector. This
//! test is executed before the main random sector test starts.
#define RUN_SMOKE_TEST 0

//! Set to 1 to invoke the short test.
#define RUN_SEQ_TEST 0

//! When this is set to 1, the main random sector read/write test will be executed.
#define RUN_RANDOM_TEST 0

//! Variant of the random read/write test that uses long sequences of sectors.
#define RUN_RANDOM_SEQ_TEST 1

//! When enabled, this will cause every sector that is written by the random test to immediately
//! be read back and compared. Regular random reads will still take place, as well.
#define DO_RANDOM_READBACK 0

//! Set to 1 to test external media instead of internal media.
#define USE_EXTERNAL_MEDIA 0

//! To enable support for multisector transactions, set this macro to 1.
#define USE_MULTI_TRANSACTIONS 1

//! Set to 1 to use the media cache instead of LDL for random tests.
#define USE_MEDIA_CACHE 1

#define NUMCACHES 8

//! Specifies the percentage (1-100%) of the total data drive to select sectors from
//! during the main random sector read/write test.
unsigned g_maxSectorRangePercent = 1;

//! Maximum number of sectors in a random sequence of the random_seq_test.
uint32_t g_maxSequenceLength = 20000;

//! Setting this to true will cause the data drive to be erased before the test begins.
bool g_eraseDriveFirst = false;

//! Set to true to force exit of the main test loop.
bool g_exitTestLoop = false;

#pragma alignvar(32)
SECTOR_BUFFER s_dataBuffer2[CACHED_BUFFER_SIZE_IN_WORDS(kMaxBufferBytes)];

#pragma alignvar(32)
SECTOR_BUFFER s_readBuffer2[CACHED_BUFFER_SIZE_IN_WORDS(kMaxBufferBytes)];

SECTOR_BUFFER * s_multiDataBuffer[2] = { (SECTOR_BUFFER *)&s_dataBuffer, (SECTOR_BUFFER *)&s_dataBuffer2 };
SECTOR_BUFFER * s_multiReadBuffer[2] = { (SECTOR_BUFFER *)&s_readBuffer, (SECTOR_BUFFER *)&s_readBuffer2 };

#if (NUMCACHES > 0)
#pragma alignvar(32)
//! Memory used by the media cache to hold sector data.
uint8_t g_mediaCacheBuffer[CACHED_BUFFER_SIZE(NOMINAL_DATA_SECTOR_SIZE * NUMCACHES)];
#endif

//! \brief Print options for random test.
typedef enum _print_options {
    kPrintSectorDetails,    //!< Print each sector number and some additional info.
    kPrintSectorDetails1PerLine,    //!< Print each sector number and some additional info.
    kPrintEachSector,   //!< Print a 'r' or 'w' for each sector.
    kPrintNSectors,     //!< Print a dot every N sectors.
    kPrintCountEveryN,  //!< Print the count every N sectors.
    kPrintNothing       //!< Produce no output during random test.
} PrintOptions_t;

PrintOptions_t g_printOption = kPrintSectorDetails1PerLine;

enum _print_config
{
    kPrintEachColumns = 128,
    kPrintNColumns = 32,
    kPrintNCount = 32,
    
    kPrintCountNModulo = 250 //2500
};

const unsigned kPatternBufferCount = 16;

/*!
 * \brief Simple bitmap class.
 */
class BitMap
{
public:
    BitMap(uint32_t count);
    ~BitMap();

    inline bool get(uint32_t n);
    inline void set(uint32_t n);
    
    bool isRangeSet(uint32_t n, uint32_t count);

protected:
    uint32_t m_count;
    uint32_t * m_bitmap;

    static uint32_t getEntryCount(uint32_t count) { return ROUND_UP_DIV(count, 32); }

};

/*!
 * \brief Random read/write stress test for a data drive.
 */
class DataDriveStressTest
{
public:
    DataDriveStressTest(DriveTag_t tag);
    
    RtStatus_t prepare_drive();
    RtStatus_t run_tests();
    
    //! \name Tests
    //@{
    RtStatus_t smoke_test(uint32_t testSector);
    RtStatus_t multi_smoke_test(uint32_t testSector);
    RtStatus_t seq_test();
    RtStatus_t random_test();
    RtStatus_t random_seq_test();
    //@}

protected:
    DriveTag_t m_tag;       //!< Tag for the drive being tested.
    LogicalDrive * m_drive; //!< Pointer to the drive object being tested.
    uint32_t m_sectorCount; //!< Total sectors in the drive.
    uint32_t m_optimalSectorCount;  //!< Sectors to use in a multisector transaction.
    
    //! Bitmap of whether the sector has been written to.
    auto_delete<BitMap> m_sectorInfo;
    
    //! \brief Statistics details for read/write transfers.
    struct TransferStatistics
    {
        uint64_t sectors;   //!< Number of sectors.
        uint64_t bytes;     //!< Total number of bytes transferred.
        uint64_t elapsed;   //!< Elapsed time in microseconds.
    };
    
    //! \brief Statistics details about one transfer direction (read or write).
    struct TransferDirectionStatistics
    {
        TransferStatistics total;
        TransferStatistics random;
        TransferStatistics sequential;
        
        void add(uint64_t elapsedTime, uint64_t byteCount, bool isSequential, unsigned sectorCount=1);
    };

    uint32_t lastSector;
    bool lastWasRead;
    int count;
    struct {
        TransferDirectionStatistics read;
        TransferDirectionStatistics write;
        TransferDirectionStatistics rewrite;
    } m_statistics;
    int z;
    char opbuf[128];
    uint32_t thisSector;
    bool isSequential;
    bool doRead;
    SimpleTimer m_totalTimer;
    uint64_t m_totalElapsedTime;
    uint64_t m_totalReadTime;
    uint64_t m_totalWriteTime;
    SECTOR_BUFFER * m_patternBuffers;
    SECTOR_BUFFER * m_sectorPatternBuffer;
    bool m_isRewrite;
    bool m_isMulti;
    uint32_t m_sequenceLength;
    
    RtStatus_t preparePatternBuffers();
    SECTOR_BUFFER * getBufferForSector(uint32_t sector);
    
    RtStatus_t random_read();
    RtStatus_t random_write();
    RtStatus_t random_multi_read();
    RtStatus_t random_multi_write();
    RtStatus_t random_cache_read();
    RtStatus_t random_cache_write();
    
    void printReport();
    void printSector();
    void printRandomSeqSector();
};

////////////////////////////////////////////////////////////////////////////////
// Prototypes
////////////////////////////////////////////////////////////////////////////////

RtStatus_t run_test();
RtStatus_t MediaCacheInit();

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

BitMap::BitMap(uint32_t count)
:   m_count(count),
    m_bitmap(0)
{
    m_bitmap = new uint32_t[getEntryCount(m_count)];
    memset(m_bitmap, 0, sizeof(uint32_t) * getEntryCount(m_count));
}

BitMap::~BitMap()
{
    if (m_bitmap)
    {
        delete [] m_bitmap;
    }
}

inline bool BitMap::get(uint32_t n)
{
    int coarse = n / 32;
    int fine = n % 32;
    return (m_bitmap[coarse] & (1 << fine)) != 0;
}

inline void BitMap::set(uint32_t n)
{
    int coarse = n / 32;
    int fine = n % 32;
    m_bitmap[coarse] |= 1 << fine;
}

bool BitMap::isRangeSet(uint32_t n, uint32_t count)
{
    for (int i = n; i < n + count; ++i)
    {
        if (!get(i))
        {
            return false;
        }
    }
    
    return true;
}

void DataDriveStressTest::TransferDirectionStatistics::add(uint64_t elapsedTime, uint64_t byteCount, bool isSequential, unsigned sectorCount)
{
    total.sectors += sectorCount;
    total.bytes += byteCount;
    total.elapsed += elapsedTime;
    
    if (isSequential)
    {
        sequential.sectors += sectorCount;
        sequential.bytes += byteCount;
        sequential.elapsed += elapsedTime;
    }
    else
    {
        random.sectors += sectorCount;
        random.bytes += byteCount;
        random.elapsed += elapsedTime;
    }
}

DataDriveStressTest::DataDriveStressTest(DriveTag_t tag)
:   m_tag(tag),
    m_drive(NULL),
    m_sectorInfo(),
    m_optimalSectorCount(0),
    lastSector(0),
    lastWasRead(false),
    count(0),
    z(0),
    thisSector(0),
    isSequential(false),
    doRead(false),
    m_totalTimer(),
    m_totalElapsedTime(0),
    m_totalReadTime(0),
    m_totalWriteTime(0),
    m_patternBuffers(NULL),
    m_sectorPatternBuffer(NULL),
    m_isRewrite(false),
    m_isMulti(false)
{
    m_drive = DriveGetDriveFromTag(tag);
    assert(m_drive);
    
    // Read drive info.
#if USE_MEDIA_CACHE
    g_actualBufferBytes = m_drive->getInfo<uint32_t>(kDriveInfoSectorSizeInBytes);
#else
    g_actualBufferBytes = m_drive->getInfo<uint32_t>(kDriveInfoNativeSectorSizeInBytes);
#endif

    m_sectorCount = m_drive->getInfo<uint32_t>(kDriveInfoSizeInNativeSectors);
    m_optimalSectorCount = m_drive->getInfo<uint32_t>(kDriveInfoOptimalTransferSectorCount);
    
    // Allocate sector usage bitmap.
    m_sectorInfo = new BitMap(m_sectorCount);
    assert(m_sectorInfo);
    
    // Clear stats.
    memset(&m_statistics, 0, sizeof(m_statistics));
    
#if USE_LIMITED_SECTOR_PATTERNS
    // Allocate pattern buffers.
    preparePatternBuffers();
#endif // USE_LIMITED_SECTOR_PATTERNS
}

RtStatus_t DataDriveStressTest::preparePatternBuffers()
{
    FASTPRINT("Preparing %d pattern buffers...\n", kPatternBufferCount);
    
    m_patternBuffers = (SECTOR_BUFFER *)malloc(g_actualBufferBytes * kPatternBufferCount);
    assert(m_patternBuffers);
    
    unsigned i;
    for (i=0; i < kPatternBufferCount; ++i)
    {
//         fill_data_buffer(getBufferForSector(i), i);

        SECTOR_BUFFER * buffer = getBufferForSector(i);
        for (uint32_t j=0; j < SIZE_IN_WORDS(g_actualBufferBytes); ++j)
        {
            buffer[j] = i | (i << 8) | (i << 16) | (i << 24);
        }
    }
    
    return SUCCESS;
}

SECTOR_BUFFER * DataDriveStressTest::getBufferForSector(uint32_t sector)
{
#if USE_LIMITED_SECTOR_PATTERNS

    // Select one of the prefilled pattern buffers to use.
    unsigned n = sector % kPatternBufferCount;
    return &m_patternBuffers[SIZE_IN_WORDS(g_actualBufferBytes) * n];

#else // USE_LIMITED_SECTOR_PATTERNS

    // Fill the data buffer with a unique pattern for this sector and return it.
    fill_data_buffer(s_dataBuffer, sector);
    return s_dataBuffer;

#endif // USE_LIMITED_SECTOR_PATTERNS
}

RtStatus_t DataDriveStressTest::prepare_drive()
{
    RtStatus_t status = SUCCESS;
    if (g_eraseDriveFirst)
    {
        FASTPRINT("Erasing test drive...\n");
        status = m_drive->erase();
        if (status != SUCCESS)
        {
            FASTPRINT("Drive erase returned 0x%08x (line %u)\n", status, __LINE__);
        }
        FASTPRINT("Finished erasing\n");
    }
    
    return status;
}

RtStatus_t DataDriveStressTest::smoke_test(uint32_t testSector)
{
    RtStatus_t status;
    
    fill_data_buffer(s_dataBuffer, testSector);
    
    // Write the pattern to the sector.
    status = m_drive->writeSector(testSector, s_dataBuffer);
    if (status != SUCCESS)
    {
        FASTPRINT("Write sector %u returned 0x%08x (line %d)\n", testSector, status, __LINE__);
        return status;
    }
    
    // Read the sector.
    status = m_drive->readSector(testSector, s_readBuffer);
    if (status != SUCCESS)
    {
        FASTPRINT("Read sector %u returned 0x%08x (line %d)\n", testSector, status, __LINE__);
        return status;
    }
    
    // Make sure we got back the data we expect.
    if (!compare_buffers(s_readBuffer, s_dataBuffer, g_actualBufferBytes))
    {
        FASTPRINT("Sector %u read compare mismatch (line %d)\n", testSector, __LINE__);
        return ERROR_GENERIC;
    }
    
    return SUCCESS;
}

RtStatus_t DataDriveStressTest::multi_smoke_test(uint32_t testSector)
{
    RtStatus_t status;
    
    fill_data_buffer(s_dataBuffer, testSector);
    fill_data_buffer(s_dataBuffer2, testSector + 1);
    
    // Write transaction.
    {
        // Open the write transaction.
        status = m_drive->openMultisectorTransaction(testSector, 2, false);
        if (status != SUCCESS)
        {
            FASTPRINT("Open transaction %u returned 0x%08x (line %d)\n", 0, status, __LINE__);
            return status;
        }
        
        // Write the pattern to the first sector.
        status = m_drive->writeSector(testSector, s_dataBuffer);
        if (status != SUCCESS)
        {
            FASTPRINT("Write sector %u returned 0x%08x (line %d)\n", testSector, status, __LINE__);
            return status;
        }
        
        // Write the pattern to the second sector.
        status = m_drive->writeSector(testSector + 1, s_dataBuffer2);
        if (status != SUCCESS)
        {
            FASTPRINT("Write sector %u returned 0x%08x (line %d)\n", testSector+1, status, __LINE__);
            return status;
        }
        
        // Commit the write transaction.
        status = m_drive->commitMultisectorTransaction();
        if (status != SUCCESS)
        {
            FASTPRINT("Commit transaction %u returned 0x%08x (line %d)\n", 0, status, __LINE__);
            return status;
        }
    }
    
    // Read transaction.
    {
        // Open the read transaction.
        status = m_drive->openMultisectorTransaction(testSector, 2, true);
        if (status != SUCCESS)
        {
            FASTPRINT("Open transaction %u returned 0x%08x (line %d)\n", 0, status, __LINE__);
            return status;
        }
        
        // Read the first sector.
        status = m_drive->readSector(testSector, s_readBuffer);
        if (status != SUCCESS)
        {
            FASTPRINT("Read sector %u returned 0x%08x (line %d)\n", testSector, status, __LINE__);
            return status;
        }
        
        // Read the second sector.
        status = m_drive->readSector(testSector+1, s_readBuffer2);
        if (status != SUCCESS)
        {
            FASTPRINT("Read sector %u returned 0x%08x (line %d)\n", testSector+1, status, __LINE__);
            return status;
        }
        
        // Commit the read transaction.
        status = m_drive->commitMultisectorTransaction();
        if (status != SUCCESS)
        {
            FASTPRINT("Commit transaction %u returned 0x%08x (line %d)\n", 0, status, __LINE__);
            return status;
        }
        
        // Make sure we got back the data we expect.
        if (!compare_buffers(s_readBuffer, s_dataBuffer, g_actualBufferBytes))
        {
            FASTPRINT("Sector %u read compare mismatch (line %d)\n", testSector, __LINE__);
            return ERROR_GENERIC;
        }
        if (!compare_buffers(s_readBuffer2, s_dataBuffer2, g_actualBufferBytes))
        {
            FASTPRINT("Sector %u read compare mismatch (line %d)\n", testSector+1, __LINE__);
            return ERROR_GENERIC;
        }
    }
    
    return SUCCESS;
}

RtStatus_t DataDriveStressTest::seq_test()
{
    RtStatus_t status;
    int i;
    int j;
    
    for (j=0; j < 64; ++j)
    {
        for (i=0; i < 512; ++i)
        {
            int actualSector = random_range(512);
            status = smoke_test(actualSector);
            if (status != SUCCESS)
            {
                FASTPRINT("Seq test failed with 0x%08x; j=%d, i=%d, sector=%d\n", status, j, i, actualSector);
                return status;
            }
        }
        
        if (j % 8 == 0)
        {
            FASTPRINT("j=%d, flushing\n", j);
            m_drive->flush();
        }
    }
    
    return SUCCESS;
}

//! Executes all of the tests that have been enabled with compile-time options.
RtStatus_t DataDriveStressTest::run_tests()
{
    RtStatus_t status;
    
    // Prep the drive.
    status = prepare_drive();
    if (status)
    {
        return status;
    }
    
#if RUN_SMOKE_TEST
    // Smoke test.
//     status = smoke_test(0);
//     if (status)
//     {
//         return status;
//     }
//     status = smoke_test(1);
//     if (status)
//     {
//         return status;
//     }

#if USE_MULTI_TRANSACTIONS
    status = multi_smoke_test(0);
    if (status)
    {
        return status;
    }
#endif // USE_MULTI_TRANSACTIONS
#endif // RUN_SMOKE_TEST

#if RUN_SEQ_TEST
    // Sequential test.
    status = seq_test();
    if (status)
    {
        return status;
    }
#endif // RUN_SEQ_TEST
    
#if RUN_RANDOM_TEST
    status = random_test();
    if (status)
    {
        return status;
    }
#endif // RUN_RANDOM_TEST
    
#if RUN_RANDOM_SEQ_TEST
    status = random_seq_test();
    if (status)
    {
        return status;
    }
#endif // RUN_RANDOM_SEQ_TEST

    return status;
}

RtStatus_t DataDriveStressTest::random_test()
{
    RtStatus_t status;
    
    FASTPRINT("Beginning test of drive 0x%02x...\n", m_tag);
    
    // Start timer for total elapsed time.
    m_totalTimer.restart();

    // Time each read or write.
    SimpleTimer transferTimer;
    
#if USE_MULTI_TRANSACTIONS
        m_isMulti = true;
#endif // USE_MULTI_TRANSACTIONS

    // Stress test.
    for (count=0; count < 1000000 && !g_exitTestLoop; ++count, transferTimer.restart())
    {
        isSequential = false;
        lastSector = thisSector;
        lastWasRead = doRead;
        
        // There's a chance that we read sequential sectors instead of totally random ones.
        // Of course, if we are at the end of the drive, we have to pick another sector.
        // There is also a small chance that we pick the same sector as last time.
        if ((lastSector < m_sectorCount - 2) && random_percent(7200)) // 72.00%
        {
            // Sequential sector.
            // m_isMulti will be set to whether the last sector was multi.
            thisSector = lastSector + (m_isMulti ? m_optimalSectorCount : 1);
            isSequential = true;
        }
        else if (random_percent(50)) // 0.50%
        {
            // Operate on same sector as last time.
            thisSector = lastSector;
        }
        else
        {
            // Select a random sector to read.
            uint32_t maxSectorRange = g_maxSectorRangePercent * m_sectorCount / 100;
            thisSector = random_range(maxSectorRange - 2);//1);
        }
        assert(thisSector < m_sectorCount);
        
#if USE_MULTI_TRANSACTIONS
        // Chance that we perform a multisector transaction. 
        m_isMulti = (thisSector < m_sectorCount - m_optimalSectorCount);// && random_percent(5000); // 50%
#endif // USE_MULTI_TRANSACTIONS

        // Fill the compare buffer with this sector's expected data.
        m_sectorPatternBuffer = getBufferForSector(thisSector);
        
        // Choose either read or write operation. If in a sequential sector, try to use the
        // same operation as the previous sector. In either case, we can read only if the
        // sector has previously been written with the test data pattern.
        if (isSequential)
        {
            doRead = (m_isMulti
                ? m_sectorInfo->isRangeSet(thisSector, m_optimalSectorCount)
                : m_sectorInfo->get(thisSector))
                && lastWasRead;
        
            if (doRead != lastWasRead)
            {
                isSequential = false;
            }
        }
        else
        {
            doRead = (m_isMulti
                ? m_sectorInfo->isRangeSet(thisSector, m_optimalSectorCount)
                : m_sectorInfo->get(thisSector))
                && random_percent(7000); // 70.00%
        }
        
        m_isRewrite = !doRead && m_sectorInfo->get(thisSector);
        
        // Perform the read or write operation.
        if (m_isMulti)
        {
            status = doRead ? random_multi_read() : random_multi_write();
        }
        else
        {
            status = doRead ? random_read() : random_write();
        }
        
        if (status != SUCCESS)
        {
            return status;
        }
        
        // Print something every few sectors.
        printSector();
        
        // Add elapsed read or write time.
        (doRead ? m_totalReadTime : m_totalWriteTime) += transferTimer;
    }
    
    // Save total elapsed time.
    m_totalElapsedTime = m_totalTimer;
    
    printReport();

    return status;
}

RtStatus_t DataDriveStressTest::random_read()
{
    RtStatus_t status;
    
    // Read the sector.
    SimpleTimer readTimer;
    status = m_drive->readSector(thisSector, s_readBuffer);
    if (status != SUCCESS)
    {
        FASTPRINT("Read sector %u returned 0x%08x (line %d)\n", thisSector, status, __LINE__);
        return status;
    }
    
    m_statistics.read.add(readTimer, g_actualBufferBytes, isSequential);
    
    // Make sure we got back the data we expect.
    if (!compare_buffers(s_readBuffer, m_sectorPatternBuffer, g_actualBufferBytes))
    {
        FASTPRINT("Sector %u read compare mismatch, count=%u (line %d)\n", thisSector, count, __LINE__);
        return ERROR_GENERIC;
    }
    
    return SUCCESS;
}

RtStatus_t DataDriveStressTest::random_write()
{
    RtStatus_t status;
    
    // Write the pattern to the sector.
    SimpleTimer writeTimer;
    status = m_drive->writeSector(thisSector, m_sectorPatternBuffer);
    if (status != SUCCESS)
    {
        FASTPRINT("Write sector %u returned 0x%08x (line %d)\n", thisSector, status, __LINE__);
        return status;
    }
    
    m_statistics.write.add(writeTimer, g_actualBufferBytes, isSequential);
    
    if (m_isRewrite)
    {
        m_statistics.rewrite.add(writeTimer, g_actualBufferBytes, isSequential);
    }
    
    // Set the flag saying that we've written to this sector.
    m_sectorInfo->set(thisSector);

#if DO_RANDOM_READBACK
    // Immediately read the sector we just wrote.
    status = m_drive->readSector(thisSector, s_readBuffer);
    if (status != SUCCESS)
    {
        FASTPRINT("Readback sector %u returned 0x%08x (line %d)\n", thisSector, status, __LINE__);
        return status;
    }
    
    // Check for all zero page. We only actually check the first two words, though.
    if (s_readBuffer[0] == 0 && s_readBuffer[1] == 0)
    {
        FASTPRINT("Readback all zeroes!? sector %u, count=%u (line %d)\n", thisSector, count, __LINE__);
    }
    
    // Make sure we got back the data we expect.
    if (!compare_buffers(s_readBuffer, m_sectorPatternBuffer, g_actualBufferBytes))
    {
        FASTPRINT("Sector %u readback compare mismatch, count=%u (line %d)\n", thisSector, count, __LINE__);
        return ERROR_GENERIC;
    }
#endif // DO_RANDOM_READBACK
    
    return SUCCESS;
}

RtStatus_t DataDriveStressTest::random_multi_read()
{
    RtStatus_t status;
    int i;
    
    // Fill the data buffers with a unique pattern for these sectors and return it.
    for (i = 0; i < m_optimalSectorCount; ++i)
    {
        memcpy(s_multiDataBuffer[i], getBufferForSector(thisSector + i), g_actualBufferBytes);
    }
    
    SimpleTimer readTimer;
    
    // Open the transaction.
    status = m_drive->openMultisectorTransaction(thisSector, m_optimalSectorCount, true);
    if (status != SUCCESS)
    {
        FASTPRINT("Open multi read %u returned 0x%08x (line %d)\n", thisSector, status, __LINE__);
        return status;
    }
    
    for (i = 0; i < m_optimalSectorCount; ++i)
    {
        // Read the sector.
        status = m_drive->readSector(thisSector + i, s_multiReadBuffer[i]);
        if (status != SUCCESS)
        {
            FASTPRINT("Multi read sector %u+%u returned 0x%08x (line %d)\n", thisSector, i, status, __LINE__);
            return status;
        }
    }
    
    // Commit the read transaction.
    status = m_drive->commitMultisectorTransaction();
    if (status != SUCCESS)
    {
        FASTPRINT("Commit multi read %u returned 0x%08x (line %d)\n", thisSector, status, __LINE__);
        return status;
    }
        
    m_statistics.read.add(readTimer, g_actualBufferBytes, isSequential, m_optimalSectorCount);
    
    // Make sure we got back the data we expect.
    for (i = 0; i < m_optimalSectorCount; ++i)
    {
        if (!compare_buffers(s_multiReadBuffer[i], s_multiDataBuffer[i], g_actualBufferBytes))
        {
            FASTPRINT("Sector %u+%u read compare mismatch, count=%u (line %d)\n", thisSector, i, count, __LINE__);
            return ERROR_GENERIC;
        }
    }
    
    return SUCCESS;
}

RtStatus_t DataDriveStressTest::random_multi_write()
{
    RtStatus_t status;
    int i;
    
    // Fill the data buffers with a unique pattern for these sectors and return it.
    for (i = 0; i < m_optimalSectorCount; ++i)
    {
        memcpy(s_multiDataBuffer[i], getBufferForSector(thisSector + i), g_actualBufferBytes);
    }

    SimpleTimer writeTimer;

    // Open the transaction.
    status = m_drive->openMultisectorTransaction(thisSector, m_optimalSectorCount, false);
    if (status != SUCCESS)
    {
        FASTPRINT("Open multi write %u returned 0x%08x (line %d)\n", thisSector, status, __LINE__);
        return status;
    }
    
    for (i = 0; i < m_optimalSectorCount; ++i)
    {
        // Write the pattern to the sector.
        status = m_drive->writeSector(thisSector + i, s_multiDataBuffer[i]);
        if (status != SUCCESS)
        {
            FASTPRINT("Write sector %u+%u returned 0x%08x (line %d)\n", thisSector, i, status, __LINE__);
            return status;
        }
        
        // Set the flag saying that we've written to this sector.
        m_sectorInfo->set(thisSector + i);
    }
    
    // Commit the read transaction.
    status = m_drive->commitMultisectorTransaction();
    if (status != SUCCESS)
    {
        FASTPRINT("Commit multi write %u returned 0x%08x (line %d)\n", thisSector, status, __LINE__);
        return status;
    }
    
    m_statistics.write.add(writeTimer, g_actualBufferBytes, isSequential, m_optimalSectorCount);
    
    if (m_isRewrite)
    {
        m_statistics.rewrite.add(writeTimer, g_actualBufferBytes, isSequential, m_optimalSectorCount);
    }

    return SUCCESS;
}

RtStatus_t DataDriveStressTest::random_cache_read()
{
    RtStatus_t status;
    
    MediaCacheParamBlock_t pb = {0};
    pb.drive = DRIVE_TAG_DATA;
    pb.sector = thisSector;
    pb.flags = kMediaCacheFlag_NoPartitionOffset;
    pb.requestSectorCount = 1;
    
    SimpleTimer readTimer;
    status = media_cache_read(&pb);
    if (status != SUCCESS)
    {
        FASTPRINT("Cache read sector %u returned 0x%08x (line %d)\n", thisSector, status, __LINE__);
        return status;
    }
    
    m_statistics.read.add(readTimer, g_actualBufferBytes, isSequential);
    
    // Make sure we got back the data we expect.
    if (!compare_buffers(pb.buffer, m_sectorPatternBuffer, g_actualBufferBytes))
    {
        FASTPRINT("Sector %u read compare mismatch, count=%u (line %d)\n", thisSector, count, __LINE__);
        return ERROR_GENERIC;
    }
    
    media_cache_release(pb.token);
    
    return SUCCESS;
}

RtStatus_t DataDriveStressTest::random_cache_write()
{
    RtStatus_t status;
    
    MediaCacheParamBlock_t pb = {0};
    pb.drive = DRIVE_TAG_DATA;
    pb.sector = thisSector;
    pb.flags = kMediaCacheFlag_NoPartitionOffset;
    pb.buffer = (uint8_t *)m_sectorPatternBuffer;
    pb.writeOffset = 0;
    pb.writeByteCount = g_actualBufferBytes;
    
    SimpleTimer writeTimer;
    status = media_cache_write(&pb);
    if (status != SUCCESS)
    {
        FASTPRINT("Cache write sector %u returned 0x%08x (line %d)\n", thisSector, status, __LINE__);
        return status;
    }
    
    m_statistics.write.add(writeTimer, g_actualBufferBytes, isSequential);
    
    if (m_isRewrite)
    {
        m_statistics.rewrite.add(writeTimer, g_actualBufferBytes, isSequential);
    }
    
    // Set the flag saying that we've written to this sector.
    m_sectorInfo->set(thisSector);
    
    return SUCCESS;
}

RtStatus_t DataDriveStressTest::random_seq_test()
{
    RtStatus_t status;
    
    // Start timer for total elapsed time.
    m_totalTimer.restart();

    // Time each read or write.
    SimpleTimer transferTimer;
    
    for (count = 0; count < 1000000 && !g_exitTestLoop; ++count)
    {
        // Pick the start sector.
        uint32_t maxSectorRange = g_maxSectorRangePercent * m_sectorCount / 100;
        thisSector = random_range(maxSectorRange - m_optimalSectorCount);
        
        // Pick the number of sectors in the sequence.
        //  10% - single sector
        //  90% - random length
        if (random_percent(1000))
        {
            m_sequenceLength = 1;
        }
        else
        {
            uint32_t maxLength = maxSectorRange - thisSector - 1;
            m_sequenceLength = std::min<uint32_t>(random_range(g_maxSequenceLength), maxLength);
        }
        
        // Pick read/write.
        doRead = random_percent(5000) && m_sectorInfo->isRangeSet(thisSector, m_sequenceLength);
        
//         if (g_printOption == kPrintSectorDetails)
//         {
            FASTPRINT("%s%u+%u [%s]\n", doRead?"r":"w", thisSector, m_sequenceLength, bytes_to_pretty_string(m_sequenceLength * g_actualBufferBytes));
//         }
        
        // Read or write this sequence of sectors.
        isSequential = false;
        uint32_t remaining = m_sequenceLength;
        while (remaining && !g_exitTestLoop)
        {
            assert(thisSector < m_sectorCount);
            m_sectorPatternBuffer = getBufferForSector(thisSector);
            m_isRewrite = !doRead && m_sectorInfo->get(thisSector);

#if USE_MEDIA_CACHE
            m_isMulti = false;
            status = doRead ? random_cache_read() : random_cache_write();
#else
            m_isMulti = (remaining >= m_optimalSectorCount);
            if (m_isMulti)
            {
                status = doRead ? random_multi_read() : random_multi_write();
            }
            else
            {
                status = doRead ? random_read() : random_write();
            }
#endif // USE_MEDIA_CACHE
            
            if (status != SUCCESS)
            {
                return status;
            }
            
            // Print something every few sectors.
//             if (g_printOption != kPrintSectorDetails)
//             {
//                 printRandomSeqSector();
//             }
            
            // Advance to next sector.
            lastSector = thisSector;
            remaining -= m_isMulti ? m_optimalSectorCount : 1;
            thisSector += m_isMulti ? m_optimalSectorCount : 1;
            isSequential = true;
            
            // Add elapsed read or write time.
            (doRead ? m_totalReadTime : m_totalWriteTime) += transferTimer;
        }
        
        lastWasRead = doRead;
    }
    
    // Save total elapsed time.
    m_totalElapsedTime = m_totalTimer;
    
    printReport();
    
    return SUCCESS;
}

void DataDriveStressTest::printReport()
{
    auto_free<char> totalTimeString = microseconds_to_pretty_string(m_totalElapsedTime); //m_statistics.write.total.elapsed + m_statistics.read.total.elapsed);
    FASTPRINT("Completed test of %llu sectors in %s\n",
        m_statistics.write.total.sectors + m_statistics.read.total.sectors,
        totalTimeString);
    
    float writeSeconds = (float)m_totalWriteTime / 1000000.0;
    float writeSectorsPerSec = (float)m_statistics.write.total.sectors / writeSeconds;
    auto_free<char> randomWriteBytesString = bytes_to_pretty_string(m_statistics.write.random.bytes);
    auto_free<char> randomWriteTimeString = microseconds_to_pretty_string(m_statistics.write.random.elapsed / m_statistics.write.random.sectors);
    auto_free<char> sequentialWriteBytesString = bytes_to_pretty_string(m_statistics.write.sequential.bytes);
    auto_free<char> sequentialWriteTimeString = microseconds_to_pretty_string(m_statistics.write.sequential.elapsed / m_statistics.write.sequential.sectors);
    auto_free<char> totalWriteBytesString = bytes_to_pretty_string(m_statistics.write.total.bytes);
    auto_free<char> totalWriteTimeString = microseconds_to_pretty_string(m_statistics.write.total.elapsed / m_statistics.write.total.sectors);
    FASTPRINT("Wrote %llu sectors @ %.2f sectors/s\n\
    Random:     %llu sectors, %s at %.2f MB/s, average %s per sector\n\
    Sequential: %llu sectors, %s at %.2f MB/s, average %s per sector\n\
    Combined:   %llu sectors, %s at %.2f MB/s, average %s per sector\n",
        m_statistics.write.total.sectors,
        writeSectorsPerSec,
        
        m_statistics.write.random.sectors,
        randomWriteBytesString,
        get_mb_s(m_statistics.write.random.bytes, m_statistics.write.random.elapsed),
        randomWriteTimeString,

        m_statistics.write.sequential.sectors,
        sequentialWriteBytesString,
        get_mb_s(m_statistics.write.sequential.bytes, m_statistics.write.sequential.elapsed),
        sequentialWriteTimeString,

        m_statistics.write.total.sectors,
        totalWriteBytesString,
        get_mb_s(m_statistics.write.total.bytes, m_statistics.write.total.elapsed),
        totalWriteTimeString
        );
    
    float readSeconds = (float)m_totalReadTime / 1000000.0;
    float readSectorsPerSec = (float)m_statistics.read.total.sectors / readSeconds;
    auto_free<char> randomReadBytesString = bytes_to_pretty_string(m_statistics.read.random.bytes);
    auto_free<char> randomReadTimeString = microseconds_to_pretty_string(m_statistics.read.random.elapsed / m_statistics.read.random.sectors);
    auto_free<char> sequentialReadBytesString = bytes_to_pretty_string(m_statistics.read.sequential.bytes);
    auto_free<char> sequentialReadTimeString = microseconds_to_pretty_string(m_statistics.read.sequential.elapsed / m_statistics.read.sequential.sectors);
    auto_free<char> totalReadBytesString = bytes_to_pretty_string(m_statistics.read.total.bytes);
    auto_free<char> totalReadTimeString = microseconds_to_pretty_string(m_statistics.read.total.elapsed / m_statistics.read.total.sectors);
    FASTPRINT("Read %llu sectors @ %.2f sectors/s:\n\
    Random:     %llu sectors, %s at %.2f MB/s, average %s per sector\n\
    Sequential: %llu sectors, %s at %.2f MB/s, average %s per sector\n\
    Combined:   %llu sectors, %s at %.2f MB/s, average %s per sector\n",
        m_statistics.read.total.sectors,
        readSectorsPerSec,
        
        m_statistics.read.random.sectors,
        randomReadBytesString,
        get_mb_s(m_statistics.read.random.bytes, m_statistics.read.random.elapsed),
        randomReadTimeString,

        m_statistics.read.sequential.sectors,
        sequentialReadBytesString,
        get_mb_s(m_statistics.read.sequential.bytes, m_statistics.read.sequential.elapsed),
        sequentialReadTimeString,

        m_statistics.read.total.sectors,
        totalReadBytesString,
        get_mb_s(m_statistics.read.total.bytes, m_statistics.read.total.elapsed),
        totalReadTimeString
        );
}

void DataDriveStressTest::printSector()
{
    switch (g_printOption)
    {
        case kPrintSectorDetails:
//             FASTPRINT("%s sector %u\n", doRead ? "Read" : "Write", thisSector);
            if (isSequential)
            {
                opbuf[z++] = m_isMulti ? '+' : '.';
                opbuf[z] = 0;
            }
            else
            {
                char sbuf[32];
                sprintf(sbuf, "%s%c%u", (z == 0 ? "" : " "), (m_isMulti ? (doRead ? 'R' : 'W') : (doRead ? 'r' : 'w')), thisSector);
                if (strlen(opbuf) + strlen(sbuf) > kPrintEachColumns)
                {
                    FASTPRINT("%s\n", opbuf);
                    z = 0;
                    opbuf[0] = 0;
                }
                strcat(opbuf, sbuf);
                z += strlen(sbuf);
            }
            if (z > kPrintEachColumns)
            {
                FASTPRINT("%s\n", opbuf);
                z = 0;
                opbuf[0] = 0;
            }
            break;
        
        case kPrintSectorDetails1PerLine:
            if (isSequential)
            {
                opbuf[z++] = m_isMulti ? '+' : '.';
                opbuf[z] = 0;
            }
            else
            {
                // Print previous line.
                if (z > 0)
                {
                    FASTPRINT("%s\n", opbuf);
                }
                
                sprintf(opbuf, "%c%u", (m_isMulti ? (doRead ? 'R' : 'W') : (doRead ? 'r' : 'w')), thisSector);
                z = strlen(opbuf);
            }
            break;
        
        case kPrintEachSector:
            opbuf[z++] = (isSequential ? (m_isMulti ? '+' : '.') : (m_isMulti ? (doRead ? 'R' : 'W') : (doRead ? 'r' : 'w')));
            opbuf[z] = 0;
            if (z > kPrintEachColumns)
            {
                FASTPRINT("%s\n", opbuf);
                z = 0;
            }
            break;
        
        case kPrintNSectors:
            if (count % kPrintNCount == 0)
            {
                opbuf[z++] = '.';
                opbuf[z] = 0;
                if (z > kPrintNColumns)
                {
                    FASTPRINT("%s\n", opbuf);
                    z = 0;
                }
            }
            break;
        
        case kPrintCountEveryN:
            if (count % kPrintCountNModulo == 0)
            {
                // 1 is added to count because this function is called before it is actually
                // incremented in the for loop but after a read/write has occurred.
                FASTPRINT("%d sectors, %llu written, %llu rewritten, %llu read\n", count+1, m_statistics.write.total.sectors, m_statistics.rewrite.total.sectors, m_statistics.read.total.sectors);
            }
            break;
        
        default:
            // Don't print anything.
    }
}

void DataDriveStressTest::printRandomSeqSector()
{
    switch (g_printOption)
    {
        case kPrintSectorDetails:
        case kPrintSectorDetails1PerLine:
            if (isSequential)
            {
                opbuf[z++] = m_isMulti ? '+' : '.';
                opbuf[z] = 0;
            }
            else
            {
                // Print previous line.
                if (z > 0)
                {
                    FASTPRINT("%s\n", opbuf);
                }
                
                sprintf(opbuf, "%c%u", (m_isMulti ? (doRead ? 'R' : 'W') : (doRead ? 'r' : 'w')), thisSector);
                z = strlen(opbuf);
            }
            break;
        
        case kPrintEachSector:
            opbuf[z++] = (isSequential ? (m_isMulti ? '+' : '.') : (m_isMulti ? (doRead ? 'R' : 'W') : (doRead ? 'r' : 'w')));
            opbuf[z] = 0;
            if (z > kPrintEachColumns)
            {
                FASTPRINT("%s\n", opbuf);
                z = 0;
            }
            break;
        
//         case kPrintNSectors:
//             if (count % kPrintNCount == 0)
//             {
//                 opbuf[z++] = '.';
//                 opbuf[z] = 0;
//                 if (z > kPrintNColumns)
//                 {
//                     FASTPRINT("%s\n", opbuf);
//                     z = 0;
//                 }
//             }
//             break;
//         
//         case kPrintCountEveryN:
//             if (count % kPrintCountNModulo == 0)
//             {
//                 // 1 is added to count because this function is called before it is actually
//                 // incremented in the for loop but after a read/write has occurred.
//                 FASTPRINT("%d sectors, %llu written, %llu rewritten, %llu read\n", count+1, m_statistics.write.total.sectors, m_statistics.rewrite.total.sectors, m_statistics.read.total.sectors);
//             }
//             break;
        
        default:
            printSector();
    }
}

#if USE_EXTERNAL_MEDIA
//! Run test on external media MMC/SD.
RtStatus_t run_test()
{
    RtStatus_t status;

    // Internal media must be initialized first.
    status = MediaInit(kInternalMedia);
    if (status != SUCCESS)
    {
        FASTPRINT("Internal media init returned 0x%08x\n", status);
        return status;
    }

    // Initialize external media.
    status = MediaInit(kExternalMedia);
    if (status != SUCCESS)
    {
        FASTPRINT("External media init returned 0x%08x\n", status);
        MediaShutdown(kInternalMedia);
        return status;
    }

    // Apply external socket power. This is normally done by the insertion detection
    // mechanism which we are not using.
    const uint32_t externalMediaNumber = 1;
    const SspPortId_t externalPortId = ddi_ssp_mmcsd_GetMediaPortId(externalMediaNumber);
    ddi_ssp_mmcsd_ControlSocketPower(externalPortId, true);
    ddi_ssp_mmcsd_EnableCmdPullup(externalPortId, true);
    ddi_ssp_mmcsd_EnableDataPullup(externalPortId, true);

    status = MediaDiscoverAllocation(kExternalMedia);
    if (status != SUCCESS)
    {
        FASTPRINT("External media discover returned 0x%08x\n", status);
        MediaShutdown(kInternalMedia);
        return status;
    }

    status = DriveInit(DRIVE_TAG_DATA_EXTERNAL);
    if (status != SUCCESS)
    {
        FASTPRINT("Initing data drive returned 0x%08x\n", status);
        MediaShutdown(kExternalMedia);
        MediaShutdown(kInternalMedia);
        return status;
    }

    DataDriveStressTest test(DRIVE_TAG_DATA_EXTERNAL);
    status = test.test_data_drive();
    if (status != SUCCESS)
    {
        FASTPRINT("test_data_drive returned 0x%08x\n", status);
        MediaShutdown(kExternalMedia);
        MediaShutdown(kInternalMedia);
        return status;
    }

    // Shutdown external media.
    status = MediaShutdown(kExternalMedia);
    if (status != SUCCESS)
    {
        FASTPRINT("External media shutdown returned 0x%08x\n", status);
        MediaShutdown(kInternalMedia);
        return status;
    }

    // Shutdown internal media.
    status = MediaShutdown(kInternalMedia);
    if (status != SUCCESS)
    {
        FASTPRINT("Internal media shutdown returned 0x%08x\n", status);
        return status;
    }

    tss_logtext_Flush(TX_WAIT_FOREVER);

    return SUCCESS;
}

#else // USE_EXTERNAL_MEDIA

//! Run test on internal media NAND or internal media eMMC/eSD.
RtStatus_t run_test()
{
    RtStatus_t status;

//     test_random_percent(1000);
//     test_random_percent(5000);
//     test_random_percent(8000);

    status = MediaInit(kInternalMedia);
    if (status != SUCCESS)
    {
        FASTPRINT("Media init returned 0x%08x\n", status);
        return status;
    }

    status = MediaDiscoverAllocation(kInternalMedia);
    if (status != SUCCESS)
    {
        FASTPRINT("Media discover returned 0x%08x\n", status);
        return status;
    }

    status = DriveInit(DRIVE_TAG_DATA);
    if (status != SUCCESS)
    {
        FASTPRINT("Initing data drive returned 0x%08x\n", status);
        MediaShutdown(kInternalMedia);
        return status;
    }

    DataDriveStressTest test(DRIVE_TAG_DATA);
    status = test.run_tests();
    if (status != SUCCESS)
    {
        FASTPRINT("test_data_drive returned 0x%08x\n", status);
        MediaShutdown(kInternalMedia);
        return status;
    }

    status = MediaShutdown(kInternalMedia);
    if (status != SUCCESS)
    {
        FASTPRINT("Media shutdown returned 0x%08x\n", status);
        return status;
    }

    tss_logtext_Flush(TX_WAIT_FOREVER);

    return SUCCESS;
}
#endif // USE_EXTERNAL_MEDIA

RtStatus_t MediaCacheInit()
{
    RtStatus_t status = SUCCESS;

#if NUMCACHES > 0
    status = media_cache_init(g_mediaCacheBuffer, sizeof(g_mediaCacheBuffer));
    if (status != SUCCESS)
    {
        FASTPRINT("media_cache_init() returned 0x%08x\n", status);
    }
#endif

    return status;
}

RtStatus_t test_main(ULONG param)
{
    RtStatus_t status;
    
    // Initialize the Media
    status = SDKInitialization();
    
#if USE_MEDIA_CACHE
    if (status == SUCCESS)
    {
        status = MediaCacheInit();
    }
#endif

    if (status == SUCCESS)
    {
        status = run_test();
    }
    
    if (status == SUCCESS)
    {
        FASTPRINT("unit test passed!\n");
    }
    else
    {
        FASTPRINT("unit test failed: 0x%08x\n", status);
    }
    
    exit(status);
    return status;
}

