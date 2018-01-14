#define OS_FILESYSTEM_FAT a_OS_FILESYSTEM_FAT(TX_NOTX)
#define a_OS_FILESYSTEM_FAT(tx) b_OS_FILESYSTEM_FAT(tx)
#define b_OS_FILESYSTEM_FAT(tx) os_filesystem_fat__##tx##.##a

