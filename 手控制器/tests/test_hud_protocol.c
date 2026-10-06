#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../shared/hud_protocol.h"

static const char *valid="{\"v\":1,\"session\":77,\"seq\":4,\"spo2_x10\":980,\"spo2_age_ms\":4500,\"voltage_mv\":3780,\"voltage_age_ms\":800,\"pitch_cdeg\":-240,\"roll_cdeg\":0,\"distance_cm\":0,\"air_age_ms\":90}";
static void replace(char *out,const char *from,const char *to)
{
    const char *at=strstr(valid,from); assert(at);
    size_t before=(size_t)(at-valid);memcpy(out,valid,before);
    strcpy(out+before,to);strcpy(out+before+strlen(to),at+strlen(from));
}
int main(void)
{
    hud_sample_t s,decoded;char buffer[HUD_MAX_JSON];
    assert(hud_decode(valid,strlen(valid),&s));
    assert(s.voltage_mv==3780&&s.roll_cdeg==0&&s.distance_cm==0&&s.pitch_cdeg==-240);
    int length=hud_encode(&s,buffer,sizeof(buffer));assert(length>0);
    assert(hud_decode(buffer,(size_t)length,&decoded));
    assert(decoded.spo2_x10==980&&decoded.spo2_age_ms==4500);
    hud_advance_age(&s,1300);assert(s.air_age_ms==1390&&s.spo2_age_ms==5800&&s.voltage_age_ms==2100);
    hud_advance_age(&s,UINT64_C(10000000));assert(s.air_age_ms==HUD_MAX_AGE_MS);
    s=hud_empty();s.session=1;length=hud_encode(&s,buffer,sizeof(buffer));
    assert(hud_decode(buffer,(size_t)length,&decoded)&&decoded.voltage_mv==HUD_NA&&decoded.pitch_cdeg==HUD_NA);
    hud_advance_age(&s,5000);assert(s.spo2_age_ms==HUD_NA);
    assert(hud_encode(&s,buffer,8)<0);
    static const char *from[]={"\"v\":1","\"session\":77","\"seq\":4","\"spo2_x10\":980","\"voltage_mv\":3780","\"air_age_ms\":90","\"pitch_cdeg\":-240","\"roll_cdeg\":0","\"distance_cm\":0","\"spo2_age_ms\":4500"};
    static const char *to[]={"\"v\":2","\"session\":0","\"seq\":4.5","\"spo2_x10\":1001","\"voltage_mv\":\"3780\"","\"air_age_ms\":-1","\"pitch_cdeg\":-18001","\"roll_cdeg\":null","\"distance_cm\":65535","\"spo2_age_ms\":null"};
    for(unsigned i=0;i<sizeof(from)/sizeof(from[0]);++i){replace(buffer,from[i],to[i]);assert(!hud_decode(buffer,strlen(buffer),&s));}
    replace(buffer,"\"v\":1","\"v\":1,\"v\":1");assert(!hud_decode(buffer,strlen(buffer),&s));
    replace(buffer,"\"v\":1","\"v\":1,\"extra\":0");assert(!hud_decode(buffer,strlen(buffer),&s));
    strcpy(buffer,valid);strcat(buffer,"{}");assert(!hud_decode(buffer,strlen(buffer),&s));
    strcpy(buffer,valid);buffer[20]=0;assert(!hud_decode(buffer,strlen(valid),&s));
    assert(!hud_decode(valid,strlen(valid)-1,&s));
    assert(hud_sequence_newer(0,UINT32_MAX));assert(!hud_sequence_newer(5,5));assert(!hud_sequence_newer(3,4));
    puts("PASS: HUD wire format, integer units, zero/null, source ages, saturation, invalid/malformed messages, sequence wrap");
}
