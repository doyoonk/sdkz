/*
 * Copyright (c) 2025 HU Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "../../../app/src/hup_server.c"

static int allocations, releases, driver_frees, packet_frees;
static bool fail_alloc, fail_driver, fail_packet, self_destroy;
static struct k_sem received, wake, command_entered, command_release, stop_called;
static atomic_int stopped, destroyed;
static void *server;
static char stack[1024];

void *palloc(size_t size) {
    if (fail_alloc) return NULL;
    allocations++;
    return malloc(size);
}
void pfree(void *p) { assert(p); releases++; free(p); }
void *init_hupacket(void *h, send_func send, void *data) {
    return fail_packet ? NULL : malloc(1);
}
void deinit_hupacket(void *h) { packet_frees++; free(h); }
void process_hupacket(void *h, uint8_t *data, size_t len) {
    if (self_destroy) deinit_hup_server(server, NULL);
    k_sem_give(&command_entered);
    k_sem_take(&command_release, K_FOREVER);
    assert(driver_frees == 0 && packet_frees == 0);
}
static void *driver_init(void *a, void *b, void *c) {
    return fail_driver ? NULL : malloc(1);
}
static void driver_stop(void *p) {
    assert(p); atomic_store(&stopped, 1);
    k_sem_give(&stop_called); k_sem_give(&wake);
}
static void driver_deinit(void *p) {
    assert(atomic_load(&stopped)); driver_frees++; free(p);
}
static int driver_recv(void *p, uint8_t *b, size_t n) {
    k_sem_give(&received);
    k_sem_take(&wake, K_FOREVER);
    assert(driver_frees == 0);
    b[0] = 'x';
    return atomic_load(&stopped) ? -ECANCELED : 1;
}
static int driver_send(void *p, const uint8_t *b, size_t n) { return n; }
static int driver_recvfrom(void *p,uint8_t *b,size_t n,struct app_peer *peer) {
    return driver_recv(p,b,n);
}
static int driver_sendto(void *p,const uint8_t *b,size_t n,const struct app_peer *peer) {
    return driver_send(p,b,n);
}
static const struct app_api api = {
    .init=driver_init, .deinit=driver_deinit, .recv=driver_recv,
    .send=driver_send, .stop=driver_stop,
};
static void reset(void) {
    allocations=releases=driver_frees=packet_frees=0;
    fail_alloc=fail_driver=fail_packet=self_destroy=false;
    atomic_store(&stopped, 0); atomic_store(&destroyed, 0);
    k_sem_reset(&received); k_sem_reset(&wake);
    k_sem_reset(&command_entered); k_sem_reset(&command_release); k_sem_reset(&stop_called);
}
static void *create(void) {
    return init_hup_server(&api, "test", stack, sizeof(stack), NULL, NULL, NULL);
}
static void *destroy(void *p) {
    deinit_hup_server(p, NULL); atomic_store(&destroyed, 1); return NULL;
}
int main(void) {
    k_sem_init(&received,0,1); k_sem_init(&wake,0,1);
    k_sem_init(&command_entered,0,1); k_sem_init(&command_release,0,1);
    k_sem_init(&stop_called,0,1);
    reset();
    assert(!init_hup_server(NULL,NULL,stack,sizeof(stack),NULL,NULL,NULL));
    assert(!init_hup_server(&api,NULL,NULL,sizeof(stack),NULL,NULL,NULL));
    assert(!init_hup_server(&api,NULL,stack,0,NULL,NULL,NULL));
    struct app_api invalid=api; invalid.stop=NULL;
    assert(!init_hup_server(&invalid,NULL,stack,sizeof(stack),NULL,NULL,NULL));
    invalid=api;invalid.recvfrom=driver_recvfrom;
    assert(!init_hup_server(&invalid,NULL,stack,sizeof(stack),NULL,NULL,NULL));
    invalid=api;invalid.sendto=driver_sendto;
    assert(!init_hup_server(&invalid,NULL,stack,sizeof(stack),NULL,NULL,NULL));
    assert(allocations==0);
    fail_alloc=true; assert(!create()); assert(releases==0);
    reset(); fail_driver=true; assert(!create());
    assert(allocations==1 && releases==1 && driver_frees==0);
    reset(); fail_packet=true; assert(!create());
    assert(allocations==1 && releases==1 && driver_frees==1 && packet_frees==0);
    reset(); server=create(); assert(server);
    k_sem_take(&received,K_FOREVER); destroy(server);
    assert(releases==1 && driver_frees==1 && packet_frees==1);
    for (int own=0; own<2; own++) {
        reset(); self_destroy=own; server=create(); assert(server);
        k_sem_take(&received,K_FOREVER); k_sem_give(&wake);
        k_sem_take(&command_entered,K_FOREVER);
        assert(releases==0 && driver_frees==0);
        pthread_t closer; assert(!pthread_create(&closer,NULL,destroy,server));
        k_sem_take(&stop_called,K_FOREVER);
        assert(!atomic_load(&destroyed));
        k_sem_give(&command_release); assert(!pthread_join(closer,NULL));
        assert(releases==1 && driver_frees==1 && packet_frees==1);
    }
    puts("PASS: invalid inputs, init failures, receive cancellation, running command drain, self-destroy guard");
    return 0;
}
