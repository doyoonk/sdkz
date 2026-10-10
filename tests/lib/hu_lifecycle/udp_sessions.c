/*
 * Copyright (c) 2025 HU Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hu/hupacket.h>
#include <zephyr/kernel.h>
#include <stdio.h>

static void echo_command(void *h,int argc,const char *argv[]);
static struct hup_cmd hup_cmd_items[]={{"echo",echo_command}};
static const size_t hup_cmd_count=1;
static struct hup_resp hup_resp_items[1];
static const size_t hup_resp_count=0;

#include "../../../lib/hu/hupacket.c"
/* Both implementation files have a private struct handle. */
#define handle udp_handle
#include "../../../lib/app/udp.c"
#undef handle
#include "../../../app/src/hup_server.c"

static atomic_int live_allocations, fail_next_allocation, command_calls;
static atomic_uint receive_calls;
static struct sockaddr_in server_addr;
static char stack[4096];

static int counted_recv(void *h,uint8_t *buffer,size_t size,struct app_peer *peer)
{
    atomic_fetch_add(&receive_calls,1);
    return _recvfrom_udp(h,buffer,size,peer);
}

void *palloc(size_t size)
{
    if (atomic_exchange(&fail_next_allocation,0)) return NULL;
    void *p=malloc(size);
    if (p) atomic_fetch_add(&live_allocations,1);
    return p;
}

void pfree(void *p)
{
    if (p) { atomic_fetch_sub(&live_allocations,1); free(p); }
}

char *itoa(int value,char *str,int base)
{
    if (base==10) sprintf(str,"%d",value);
    else { assert(base==16); sprintf(str,"%x",(unsigned)value); }
    return str;
}

static void echo_command(void *h,int argc,const char *argv[])
{
    assert(argc==2);
    atomic_fetch_add(&command_calls,1);
    hupacket_ack_response(h,NULL);
    hupacket_record_str(h,NULL,argv[1]);
    assert(hupacket_send_buffer(h,NULL)>0);
}

static int client_socket(uint32_t address,uint16_t port)
{
    int fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    assert(fd>=0);
    struct sockaddr_in addr={.sin_family=AF_INET,.sin_port=port,
        .sin_addr={.s_addr=htonl(address)}};
    assert(!bind(fd,(struct sockaddr *)&addr,sizeof(addr)));
    struct timeval timeout={.tv_sec=0,.tv_usec=200000};
    assert(!setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout)));
    return fd;
}

static void send_bytes(int fd,const char *data,size_t size)
{
    assert(sendto(fd,data,size,0,(struct sockaddr *)&server_addr,sizeof(server_addr))==(ssize_t)size);
}

static void send_text(int fd,const char *data) { send_bytes(fd,data,strlen(data)); }

static size_t packet(char *out,unsigned char start,const char *body,bool crc)
{
    int n;
    if (crc) n=snprintf(out,256,"%c%s\32%x\4",start,body,
        crc16(CRC16_CCITT_POLY,0,(const uint8_t *)body,strlen(body)));
    else n=snprintf(out,256,"%c%s\4",start,body);
    assert(n>0 && n<256);
    return n;
}

static void request(int fd,const char *body,bool crc)
{
    char out[256];size_t n=packet(out,5,body,crc);send_bytes(fd,out,n);
}

static void reply(int fd,unsigned char status,const char *body,bool crc)
{
    char expected[256],actual[256];size_t len=packet(expected,status,body,crc);
    ssize_t n=recv(fd,actual,sizeof(actual),0);
    assert(n==(ssize_t)len && !memcmp(expected,actual,len));
}

static void no_reply(int fd)
{
    char buf[256];assert(recv(fd,buf,sizeof(buf),0)<0 && (errno==EAGAIN || errno==EWOULDBLOCK));
}

static void expire_all(void)
{
    /* Observe the next receive to ensure the previous command has returned
     * and the server has updated its activity timestamp after sending. */
    unsigned calls=atomic_load(&receive_calls);
    for(int i=0;i<300 && atomic_load(&receive_calls)==calls;i++) k_msleep(10);
    assert(atomic_load(&receive_calls)!=calls);
    atomic_fetch_add(&test_time_offset,CONFIG_HU_UDP_SESSION_TIMEOUT_MS+1);
    /* One allocation remains for the server and its bounded session table. */
    for(int i=0;i<300 && atomic_load(&live_allocations)!=1;i++) k_msleep(10);
    assert(atomic_load(&live_allocations)==1);
}

int main(void)
{
    struct app_api api=udp_server;api.recvfrom=counted_recv;
    struct handle *server=init_hup_server(&api,"sessions",stack,sizeof(stack),NULL,NULL,NULL);
    assert(server && atomic_load(&live_allocations)==1);
    struct udp_handle *udp=server->drv;
    socklen_t addr_len=sizeof(server_addr);
    assert(!getsockname(udp->sock,(struct sockaddr *)&server_addr,&addr_len));
    server_addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    int a=client_socket(0x7f000001,0),b=client_socket(0x7f000001,0);

    /* Same IP, distinct ports: interleave partial packets and CRC bytes. */
    send_text(a,"\5" "19@echo:7\36A");
    char b_packet[256];size_t b_len=packet(b_packet,5,"19@echo:7\36B",true);
    send_bytes(b,b_packet,b_len-3);
    send_text(a,"-tail\4");reply(a,6,"19@echo:7\36" "0\36A-tail",false);
    send_bytes(b,b_packet+b_len-3,3);reply(b,6,"19@echo:7\36" "0\36B",true);
    assert(atomic_load(&command_calls)==2);
    request(b,"19@echo:7\36B",true);reply(b,6,"19@echo:7\36" "0\36B",true);
    request(b,"19@echo:7\36B",true);reply(b,6,"19@echo:7\36" "0\36B",true);
    assert(atomic_load(&command_calls)==4);
    puts("PASS: interleaved senders, CRC state, reply routing, repeated sequences");

    /* Same port, distinct IPs must also select different parsers. */
    struct sockaddr_in a_addr;socklen_t a_len=sizeof(a_addr);
    assert(!getsockname(a,(struct sockaddr *)&a_addr,&a_len));
    int c=client_socket(0x7f000002,a_addr.sin_port);
    send_text(a,"\5" "19@echo:7\36A");send_text(c,"injected\4");
    no_reply(c);no_reply(a);
    request(c,"19@echo:7\36C",false);reply(c,6,"19@echo:7\36" "0\36C",false);
    send_text(a,"-safe\4");reply(a,6,"19@echo:7\36" "0\36A-safe",false);

    int d=client_socket(0x7f000001,0),e=client_socket(0x7f000001,0);
    request(d,"19@echo:7\36D",false);reply(d,6,"19@echo:7\36" "0\36D",false);
    assert(atomic_load(&live_allocations)==1+CONFIG_HU_UDP_MAX_SESSIONS);
    send_text(a,"\5" "19@echo:7\36keep");
    request(e,"19@echo:7\36E",false);no_reply(e);
    send_text(a,"-state\4");reply(a,6,"19@echo:7\36" "0\36keep-state",false);
    puts("PASS: IP/port key and full table preserves existing senders");

    send_text(a,"\5" "19@echo:7\36old");
    /* A complete B response confirms the preceding A datagram was consumed. */
    request(b,"19@echo:7\36barrier",false);reply(b,6,"19@echo:7\36" "0\36barrier",false);
    expire_all();
    send_text(a,"-expired\4");no_reply(a);
    request(a,"19@echo:7\36fresh",false);reply(a,6,"19@echo:7\36" "0\36fresh",false);
    request(e,"19@echo:7\36E",false);reply(e,6,"19@echo:7\36" "0\36E",false);
    puts("PASS: idle expiry without traffic, stale fragments discarded, slots reusable");

    expire_all();atomic_store(&fail_next_allocation,1);
    request(e,"19@echo:7\36failed",false);no_reply(e);
    assert(atomic_load(&live_allocations)==1);
    request(e,"19@echo:7\36retry",false);reply(e,6,"19@echo:7\36" "0\36retry",false);
    request(e,"19@missing:7",false);
    char expected[64];snprintf(expected,sizeof(expected),"19@missing:7\36%d",-ENOSYS);
    reply(e,21,expected,false);
    send_text(a,"\5" "19@echo:7\36unfinished");
    request(e,"19@echo:7\36barrier",false);reply(e,6,"19@echo:7\36" "0\36barrier",false);
    deinit_hup_server(server,NULL);
    assert(atomic_load(&live_allocations)==0);
    close(a);close(b);close(c);close(d);close(e);
    puts("PASS: allocation failure recovery, unknown-command response, session teardown");
    return 0;
}
