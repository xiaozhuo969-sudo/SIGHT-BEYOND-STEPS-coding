"""Exercise the production DNS parser with malformed datagrams under sanitizers."""
from pathlib import Path
import subprocess
import tempfile
source = (Path(__file__).parents[1] / 'components/dns_server/dns_server.c').read_text()
parser = source[source.index('static int parse_dns_request'):source.index('/*\n    Sets up a socket')]
harness = r'''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <arpa/inet.h>
typedef struct { uint32_t addr; } esp_ip4_addr_t;
typedef void esp_netif_t;
typedef struct { esp_ip4_addr_t ip; } esp_netif_ip_info_t;
#define ESP_OK 0
static esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key) { return NULL; }
static int esp_netif_get_ip_info(esp_netif_t *n, esp_netif_ip_info_t *i) { return -1; }
typedef struct __attribute__((packed)) { uint16_t ptr_offset,type,class; uint32_t ttl; uint16_t addr_len; uint32_t ip_addr; } dns_answer_t;
typedef struct { int num_of_entries; struct { const char *name,*if_key; esp_ip4_addr_t ip; } entry[1]; } handle_t;
typedef handle_t *dns_server_handle_t;
'''
main = r'''
int main(void) {
    handle_t h = {.num_of_entries=1,.entry={{.name="*",.ip={.addr=0x0104a8c0}}}};
    unsigned char valid[]={0x12,0x34,1,0,0,1,0,0,0,0,0,0,3,'c','a','m',5,'l','o','c','a','l',0,0,1,0,1};
    char out[256];
    int n=parse_dns_request((char*)valid,sizeof(valid),out,sizeof(out),&h);
    assert(n==sizeof(valid)+16 && out[7]==1);
    valid[24]=28; n=parse_dns_request((char*)valid,sizeof(valid),out,sizeof(out),&h);
    assert(n==sizeof(valid) && out[7]==0);
    valid[24]=1;
    for(size_t len=0;len<sizeof(valid);len++) {
        char *p=malloc(len?len:1);memcpy(p,valid,len);
        assert(parse_dns_request(p,len,out,sizeof(out),&h)<0);free(p);
    }
    srand(1234);
    for(int i=0;i<100000;i++) {
        size_t len=rand()%257;char *p=malloc(len?len:1);
        for(size_t j=0;j<len;j++) p[j]=rand();
        if(len>=12) {p[2]=1;p[4]=0;p[5]=1;}
        n=parse_dns_request(p,len,out,sizeof(out),&h);
        assert(n<=(int)sizeof(out));free(p);
    }
    return 0;
}
'''
with tempfile.TemporaryDirectory() as folder:
    p=Path(folder);(p/'test.c').write_text(harness+parser+main)
    subprocess.run(['cc','-fsanitize=address,undefined','-g',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('PASS: A/AAAA answers, all truncated prefixes, 100000 malformed DNS packets (ASan/UBSan)')
