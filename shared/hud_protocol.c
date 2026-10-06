#include "hud_protocol.h"
#include <math.h>
#include <string.h>
#include "cJSON.h"

hud_sample_t hud_empty(void)
{
    return (hud_sample_t){.spo2_x10=HUD_NA,.spo2_age_ms=HUD_NA,
        .voltage_mv=HUD_NA,.voltage_age_ms=HUD_NA,
        .pitch_cdeg=HUD_NA,.roll_cdeg=HUD_NA,.distance_cm=HUD_NA,.air_age_ms=HUD_NA};
}
int32_t hud_age(uint64_t ms) { return ms > HUD_MAX_AGE_MS ? HUD_MAX_AGE_MS : (int32_t)ms; }
void hud_advance_age(hud_sample_t *s, uint64_t ms)
{
    if (s->spo2_age_ms != HUD_NA) s->spo2_age_ms = hud_age(s->spo2_age_ms + ms);
    if (s->voltage_age_ms != HUD_NA) s->voltage_age_ms = hud_age(s->voltage_age_ms + ms);
    if (s->air_age_ms != HUD_NA) s->air_age_ms = hud_age(s->air_age_ms + ms);
}
bool hud_sequence_newer(uint32_t seq, uint32_t previous)
{
    uint32_t delta = seq - previous;
    return delta != 0 && delta < UINT32_C(0x80000000);
}
static bool integer(const cJSON *item, double lo, double hi)
{
    return cJSON_IsNumber(item) && isfinite(item->valuedouble) &&
        item->valuedouble >= lo && item->valuedouble <= hi && trunc(item->valuedouble) == item->valuedouble;
}
static bool optional(const cJSON *root, const char *name, int lo, int hi, int32_t *out)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(root, name);
    if (cJSON_IsNull(value)) { *out=HUD_NA; return true; }
    if (!integer(value,lo,hi)) return false;
    *out=(int32_t)value->valuedouble; return true;
}
bool hud_decode(const char *buffer, size_t length, hud_sample_t *out)
{
    if (!buffer || !out || !length || length >= HUD_MAX_JSON || memchr(buffer,0,length)) return false;
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(buffer,length,&end,false);
    if (!root) return false;
    while (end < buffer+length && (*end==' ' || *end=='\r' || *end=='\n' || *end=='\t')) ++end;
    bool ok=cJSON_IsObject(root) && end==buffer+length && cJSON_GetArraySize(root)==11;
    /* Reject duplicate members as well as unknown/extra fields. */
    for (const cJSON *a=root->child; ok && a; a=a->next)
        for (const cJSON *b=a->next; b; b=b->next)
            if (!a->string || !b->string || !strcmp(a->string,b->string)) { ok=false; break; }
    hud_sample_t s=hud_empty();
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(root,"v"),
        *session=cJSON_GetObjectItemCaseSensitive(root,"session"),
        *seq=cJSON_GetObjectItemCaseSensitive(root,"seq");
    ok=ok && integer(v,HUD_VERSION,HUD_VERSION) && integer(session,1,UINT32_MAX) &&
        integer(seq,0,UINT32_MAX) &&
        optional(root,"spo2_x10",0,1000,&s.spo2_x10) &&
        optional(root,"spo2_age_ms",0,HUD_MAX_AGE_MS,&s.spo2_age_ms) &&
        optional(root,"voltage_mv",0,6000,&s.voltage_mv) &&
        optional(root,"voltage_age_ms",0,HUD_MAX_AGE_MS,&s.voltage_age_ms) &&
        optional(root,"pitch_cdeg",-18000,18000,&s.pitch_cdeg) &&
        optional(root,"roll_cdeg",-18000,18000,&s.roll_cdeg) &&
        optional(root,"distance_cm",0,65534,&s.distance_cm) &&
        optional(root,"air_age_ms",0,HUD_MAX_AGE_MS,&s.air_age_ms);
    if (ok) {
        s.session=(uint32_t)session->valuedouble; s.seq=(uint32_t)seq->valuedouble;
        ok=(s.spo2_x10==HUD_NA || s.spo2_age_ms!=HUD_NA) &&
            (s.voltage_mv==HUD_NA || s.voltage_age_ms!=HUD_NA) &&
            ((s.pitch_cdeg==HUD_NA)==(s.roll_cdeg==HUD_NA)) &&
            ((s.pitch_cdeg==HUD_NA && s.distance_cm==HUD_NA) || s.air_age_ms!=HUD_NA);
    }
    cJSON_Delete(root);
    if (ok) *out=s;
    return ok;
}
static bool add(cJSON *root, const char *key, int32_t value)
{
    return value==HUD_NA ? cJSON_AddNullToObject(root,key)!=NULL : cJSON_AddNumberToObject(root,key,value)!=NULL;
}
int hud_encode(const hud_sample_t *s, char *buffer, size_t capacity)
{
    if (!s || !buffer || capacity<2 || capacity>INT32_MAX || !s->session) return -1;
    cJSON *root=cJSON_CreateObject(); if (!root) return -1;
    bool ok=cJSON_AddNumberToObject(root,"v",HUD_VERSION) &&
        cJSON_AddNumberToObject(root,"session",s->session) && cJSON_AddNumberToObject(root,"seq",s->seq) &&
        add(root,"spo2_x10",s->spo2_x10) && add(root,"spo2_age_ms",s->spo2_age_ms) &&
        add(root,"voltage_mv",s->voltage_mv) &&
        add(root,"voltage_age_ms",s->voltage_age_ms) &&
        add(root,"pitch_cdeg",s->pitch_cdeg) && add(root,"roll_cdeg",s->roll_cdeg) &&
        add(root,"distance_cm",s->distance_cm) && add(root,"air_age_ms",s->air_age_ms) &&
        cJSON_PrintPreallocated(root,buffer,(int)capacity,false);
    cJSON_Delete(root);
    return ok ? (int)strlen(buffer) : -1;
}
