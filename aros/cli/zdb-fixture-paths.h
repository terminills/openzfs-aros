/* Test-only paths follow partition.library PT_POSITION, not disk slot numbers. */
#ifdef OZ_ZDB_GPT
/* GPT uses populated-entry indices even when the on-disk slots have gaps. */
#define OZ_ZDB_SECOND_PATH "/AROS/ata.device/0/partition/1"
#define OZ_ZDB_MISSING_PATH "/AROS/ata.device/0/partition/2"
#else
#define OZ_ZDB_SECOND_PATH "/AROS/ata.device/0/partition/2"
#define OZ_ZDB_MISSING_PATH "/AROS/ata.device/0/partition/1"
#endif
