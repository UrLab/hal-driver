#include "lighttest2.h"
#include "../HALMsg.h"

TEST(checksum, {
    HALMsg msg;
    memset(&msg, 0, sizeof(msg));

    msg.cmd = VERSION;
    ASSERT(IS(msg, VERSION));
    ASSERT(HALMsg_checksum(&msg) == VERSION);

    msg.data[0] = 1;
    ASSERT(HALMsg_checksum(&msg) == VERSION); //len is zero -> dont check data

    msg.len = 1;
    ASSERT(HALMsg_checksum(&msg) == VERSION+2); //+len +data
})

SUITE(
    ADDTEST(checksum)
)