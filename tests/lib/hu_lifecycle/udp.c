/*
 * Copyright (c) 2025 HU Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "../../../lib/app/udp.c"
#include <time.h>

static struct k_sem entered;
static void *receiver(void *p) {
    uint8_t buf[16]; k_sem_give(&entered);
    struct app_peer peer;
    int rc;
    do { rc=_recvfrom_udp(p,buf,sizeof(buf),&peer); } while(rc==0);
    assert(rc==-ECANCELED); return NULL;
}
int main(void) {
    struct handle *h=_init_udp(NULL,NULL,NULL); assert(h);
    struct sockaddr_in addr; socklen_t len=sizeof(addr);
    assert(!getsockname(h->sock,(struct sockaddr *)&addr,&len));
    addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    int client=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP); assert(client>=0);
    uint8_t buf[16];
    struct app_peer peer;
    assert(sendto(client,"test",4,0,(struct sockaddr *)&addr,len)==4);
    assert(_recvfrom_udp(h,buf,sizeof(buf),&peer)==4 && !memcmp(buf,"test",4));
    assert(_sendto_udp(h,buf,4,&peer)==4);
    assert(recv(client,buf,sizeof(buf),0)==4 && !memcmp(buf,"test",4));
    k_sem_init(&entered,0,1);
    pthread_t thread; assert(!pthread_create(&thread,NULL,receiver,h));
    k_sem_take(&entered,K_FOREVER); k_msleep(20);
    struct timespec start,end; clock_gettime(CLOCK_MONOTONIC,&start);
    _stop_udp(h); assert(!pthread_join(thread,NULL));
    clock_gettime(CLOCK_MONOTONIC,&end);
    assert((end.tv_sec-start.tv_sec)*1000000000LL+end.tv_nsec-start.tv_nsec<1000000000LL);
    assert(_sendto_udp(h,buf,4,&peer)==-ECANCELED);
    _deinit_udp(h); close(client);
    puts("PASS: UDP loopback and cancellation of blocked receive within 1 second");
    return 0;
}
