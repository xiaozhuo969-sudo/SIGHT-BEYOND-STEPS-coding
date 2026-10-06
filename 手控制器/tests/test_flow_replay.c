/* Feed the actual emulated STM32 radio packets into the actual hand parser.
 * File I/O only. This executable has no hardware or serial code. */
#include <assert.h>
#include <stdio.h>
#include "tx_control.h"
#include "flow_diag_log.h"
int main(int argc,char **argv) {
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
    tx_control_t s={0};assert(tx_start_v3(&s,1000,123));
    unsigned char record[36];unsigned count=0;
    while(fread(record,1,36,f)==36) {
        uint32_t now=(uint32_t)record[0]|((uint32_t)record[1]<<8)|((uint32_t)record[2]<<16)|((uint32_t)record[3]<<24);
        s.sequence=((uint16_t)record[12]<<8)|record[13];
        tx_feedback(&s,record+4,now);flow_diag_log(&s.flow,s.session,now);
        assert(s.phase==TX_READY);assert(s.air_flags==0);++count;
    }
    assert(feof(f));fclose(f);
    assert(count==81 && s.air_frames==41 && s.flow.updates==40);
    assert(!s.rejected_schema && !s.rejected_sequence && !s.rejected_session);
    assert(s.flow.mask==31 && !s.flow.incomplete);
    assert(s.flow.page[0][0]>>8==80);
    assert(s.flow.page[4][0]==1 && (int16_t)s.flow.page[4][1]==-1);
    assert(s.flow.page[3][2]==0 && s.flow.page[3][3]==0 && s.flow.page[3][4]==0 && s.flow.page[3][5]==0);
    puts("ACTUAL_ELF_TO_HAND_REPLAY_PASS");return 0;
}
