/*
 * Copyright (c) 2025 HU Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "../../../lib/app/uart.c"

static struct device dev={"mock-uart",true};
static uart_callback_t async_cb;
static uart_irq_callback_user_data_t irq_cb;
static void *async_cookie, *irq_cookie;
static int callback_error, rx_error;
static bool gate_irq, delayed_dma;
static atomic_int rx_irq, tx_irq, destroyed;
static struct k_sem entered, release_irq, rx_disabled, tx_aborted, detached;
static struct k_sem io_entered;
static bool fifo_mode, gate_resume;
static int fifo_result=1, fifo_reads, irq_updates;
static uint8_t fifo_data[1600], tx_output[64];
static size_t fifo_size, fifo_offset, tx_size;
static struct k_sem resume_entered, resume_release;

int uart_callback_set(const struct device *d,uart_callback_t cb,void *data) {
    if(cb && callback_error)return callback_error;
    async_cb=cb;async_cookie=data;return 0;
}
int uart_irq_callback_user_data_set(const struct device *d,uart_irq_callback_user_data_t cb,void *data) {
    if(cb && callback_error)return callback_error;
    irq_cb=cb;irq_cookie=data;if(!cb)k_sem_give(&detached);return 0;
}
int uart_rx_enable(const struct device *d,uint8_t *b,size_t n,int timeout) { return rx_error; }
int uart_rx_disable(const struct device *d) {
    if (!delayed_dma) {
        struct uart_event e={.type=UART_RX_DISABLED};async_cb(d,&e,async_cookie);
    }
    k_sem_give(&rx_disabled);return 0;
}
int uart_tx_abort(const struct device *d) {
    k_sem_give(&tx_aborted);return delayed_dma ? 0 : -EFAULT;
}
int uart_rx_buf_rsp(const struct device *d,uint8_t *b,size_t n) { return 0; }
int uart_tx(const struct device *d,const uint8_t *b,size_t n,int timeout) { return 0; }
int uart_irq_update(const struct device *d) {
    if(gate_irq) { k_sem_give(&entered);k_sem_take(&release_irq,K_FOREVER); }
    if(fifo_mode) assert(++irq_updates<100); /* Bound each callback deterministically. */
    return 1;
}
int uart_irq_is_pending(const struct device *d) {
    return fifo_mode && ((atomic_load(&rx_irq) && fifo_offset<fifo_size) || atomic_load(&tx_irq));
}
/* Like STM32, RX-ready may remain set even when RX interrupts are disabled. */
int uart_irq_rx_ready(const struct device *d) { return fifo_mode && fifo_offset<fifo_size; }
int uart_irq_tx_ready(const struct device *d) { return fifo_mode && atomic_load(&tx_irq); }
int uart_fifo_read(const struct device *d,uint8_t *b,int n) {
    assert(n>0);fifo_reads++;
    if(fifo_result<=0)return fifo_result;
    n=MIN((size_t)n,fifo_size-fifo_offset);
    memcpy(b,fifo_data+fifo_offset,n);fifo_offset+=n;return n;
}
int uart_fifo_fill(const struct device *d,const uint8_t *b,int n) {
    if(fifo_mode) { assert(tx_size+n<=sizeof(tx_output));memcpy(tx_output+tx_size,b,n);tx_size+=n; }
    return n;
}
void uart_irq_rx_enable(const struct device *d) {
    if(gate_resume) { k_sem_give(&resume_entered);k_sem_take(&resume_release,K_FOREVER); }
    atomic_store(&rx_irq,1);
}
void uart_irq_tx_enable(const struct device *d) { atomic_store(&tx_irq,1); }
void uart_irq_rx_disable(const struct device *d) { atomic_store(&rx_irq,0); }
void uart_irq_tx_disable(const struct device *d) { atomic_store(&tx_irq,0); }
int uart_poll_in(const struct device *d,uint8_t *b) { return -1; }
void uart_poll_out(const struct device *d,uint8_t b) {}

static void *receive(void *p) {
    uint8_t b[8]; k_sem_give(&io_entered);
    assert(_recv_async_int(p,b,sizeof(b))==-ECANCELED);return NULL;
}
static void *send_full(void *p) {
    uint8_t b[TX_BUF_SIZE+1]={0};k_sem_give(&io_entered);
    assert(_send_int(p,b,sizeof(b))==-ECANCELED);return NULL;
}
static void *invoke_irq(void *p) { _irq_cb(&dev,p);return NULL; }
static void *destroy(void *p) {
    _deinit_uart(p);atomic_store(&destroyed,1);return NULL;
}
static void *consume_one(void *p) {
    uint8_t byte;assert(_recv_async_int(p,&byte,1)==1);return NULL;
}
static void *stop(void *p) { _stop_uart(p);return NULL; }
static void run_irq(void) { irq_updates=0;irq_cb(&dev,irq_cookie); }
static void prepare_fifo(size_t size) {
    fifo_mode=true;fifo_size=size;fifo_offset=tx_size=0;fifo_reads=0;fifo_result=1;
    for(size_t i=0;i<size;i++)fifo_data[i]=(uint8_t)(i*13+17);
}
static void test_full_rx(void) {
    struct handle *h=_init_int(&dev,NULL,NULL);assert(h);
    prepare_fifo(1537);run_irq();
    assert(fifo_offset==RX_BUF_SIZE && fifo_reads==RX_BUF_SIZE/UART_FIFO_SIZE);
    assert(h->rx_paused && !atomic_load(&rx_irq));
    int reads=fifo_reads;
    uint8_t reply[]={1,2,3,4,5};
    assert(_send_int(h,reply,sizeof(reply))==0);run_irq();
    assert(fifo_reads==reads && tx_size==sizeof(reply) && !memcmp(tx_output,reply,sizeof(reply)));
    assert(h->rx_paused && !atomic_load(&rx_irq) && !atomic_load(&tx_irq));

    uint8_t received[sizeof(fifo_data)];size_t total=0;
    assert(_recv_async_int(h,received,0)==0 && h->rx_paused);
    while(total<fifo_size) {
        size_t chunk=MIN(13,fifo_size-total);
        int n=_recv_async_int(h,received+total,chunk);assert(n>0);
        total+=n;assert(atomic_load(&rx_irq));run_irq();
    }
    assert(total==fifo_size && !memcmp(received,fifo_data,total));
    assert(!h->rx_paused && ring_buf_is_empty(&h->rx_buffer));
    _deinit_uart(h);fifo_mode=false;
    puts("PASS: 1537-byte burst, full RX pause/resume, ordered delivery, TX while RX paused, zero-length read");
}
static void test_no_progress(void) {
    for(int result=0;result>=-1;result--) {
        struct handle *h=_init_int(&dev,NULL,NULL);assert(h);
        prepare_fifo(1);fifo_result=result;run_irq();
        assert(fifo_reads==1 && irq_updates==1 && ring_buf_is_empty(&h->rx_buffer));
        fifo_result=1;run_irq();uint8_t byte;
        assert(_recv_async_int(h,&byte,1)==1 && byte==fifo_data[0]);
        _deinit_uart(h);fifo_mode=false;
    }
    puts("PASS: zero/error FIFO reads exit callback and later data is accepted");
}
static void test_stop_during_resume(void) {
    struct handle *h=_init_int(&dev,NULL,NULL);assert(h);
    prepare_fifo(RX_BUF_SIZE+1);run_irq();assert(h->rx_paused);
    gate_resume=true;pthread_t consumer,stopper;
    assert(!pthread_create(&consumer,NULL,consume_one,h));
    k_sem_take(&resume_entered,K_FOREVER);
    assert(!pthread_create(&stopper,NULL,stop,h));
    for(int i=0;i<1000 && !atomic_get(&h->stopping);i++)k_msleep(1);
    assert(atomic_get(&h->stopping));
    k_sem_give(&resume_release);
    assert(!pthread_join(consumer,NULL));assert(!pthread_join(stopper,NULL));
    gate_resume=false;assert(!atomic_load(&rx_irq) && h->rx_paused);
    uint8_t byte;assert(_read_irq_buffer(h,&byte,1)==1 && !atomic_load(&rx_irq));
    _deinit_uart(h);fifo_mode=false;
    puts("PASS: shutdown serialized with RX resume; no interrupt re-enabled after stop");
}
int main(void) {
    k_sem_init(&entered,0,1);k_sem_init(&release_irq,0,1);
    k_sem_init(&rx_disabled,0,1);k_sem_init(&tx_aborted,0,1);
    k_sem_init(&detached,0,1);k_sem_init(&io_entered,0,1);
    k_sem_init(&resume_entered,0,1);k_sem_init(&resume_release,0,1);
    assert(!_init_int(NULL,NULL,NULL));
    callback_error=-ENOTSUP;
    assert(!_init_int(&dev,NULL,NULL));assert(!_init_async(&dev,NULL,NULL));
    assert(callback_handles.head==NULL);callback_error=0;
    rx_error=-EIO;assert(!_init_async(&dev,NULL,NULL));rx_error=0;
    assert(async_cb==NULL && callback_handles.head==NULL);

    for(int sending=0;sending<2;sending++) {
        struct handle *h=_init_int(&dev,NULL,NULL);assert(h);
        assert(!_init_int(&dev,NULL,NULL));
        pthread_t io;assert(!pthread_create(&io,NULL,sending ? send_full : receive,h));
        k_sem_take(&io_entered,K_FOREVER);k_msleep(10);
        _stop_uart(h);assert(!pthread_join(io,NULL));
        uart_irq_callback_user_data_t late=irq_cb;
        void *late_cookie=irq_cookie;
        _deinit_uart(h);assert(!irq_cb && !atomic_load(&rx_irq) && !atomic_load(&tx_irq));
        late(&dev,late_cookie);assert(callback_handles.head==NULL);
    }

    struct handle *h=_init_int(&dev,NULL,NULL);assert(h);
    gate_irq=true;k_sem_reset(&detached);atomic_store(&destroyed,0);
    pthread_t callback,closer;
    assert(!pthread_create(&callback,NULL,invoke_irq,irq_cookie));
    k_sem_take(&entered,K_FOREVER);
    assert(!pthread_create(&closer,NULL,destroy,h));
    k_sem_take(&detached,K_FOREVER);assert(!atomic_load(&destroyed));
    k_sem_give(&release_irq);assert(!pthread_join(callback,NULL));
    assert(!pthread_join(closer,NULL));gate_irq=false;
    assert(callback_handles.head==NULL);

    h=_init_async(&dev,NULL,NULL);assert(h);delayed_dma=true;
    uint8_t b=42;assert(_send_async(h,&b,1)==0);assert(atomic_get(&h->tx_active));
    atomic_store(&destroyed,0);k_sem_reset(&rx_disabled);k_sem_reset(&tx_aborted);
    uart_callback_t late_async=async_cb;
    void *late_cookie=async_cookie;
    assert(!pthread_create(&closer,NULL,destroy,h));
    k_sem_take(&rx_disabled,K_FOREVER);assert(!atomic_load(&destroyed));
    struct uart_event e={.type=UART_RX_DISABLED};late_async(&dev,&e,late_cookie);
    k_sem_take(&tx_aborted,K_FOREVER);assert(!atomic_load(&destroyed));
    e.type=UART_TX_ABORTED;e.data.tx.len=1;late_async(&dev,&e,late_cookie);
    assert(!pthread_join(closer,NULL));assert(!async_cb && callback_handles.head==NULL);
    late_async(&dev,&e,late_cookie);
    h=_init_async(&dev,NULL,NULL);assert(h && atomic_get(&h->rx_active));
    e.type=UART_RX_DISABLED;late_async(&dev,&e,late_cookie);
    assert(atomic_get(&h->rx_active)); /* Old events cannot touch a new owner. */
    delayed_dma=false;_deinit_uart(h);

    h=_init_poll(&dev,NULL,NULL);assert(h);_stop_uart(h);
    assert(_recv_poll(h,&b,1)==-ECANCELED && _send_poll(h,&b,1)==-ECANCELED);
    _deinit_uart(h);assert(callback_handles.head==NULL);
    test_full_rx();test_no_progress();test_stop_during_resume();
    puts("PASS: init failure, ownership, RX/TX cancellation, active/late callbacks, DMA drain, polling stop");
    return 0;
}
