/*
 * Copyright (c) 2025 HU Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hu/hupacket.h>
#include <zephyr/kernel.h>
#include <stdio.h>
#include <limits.h>

static void sink(void *,int,const char *[]);
static void many(void *,int,const char *[]);
static void version(void *,int,const char *[]);
static struct hup_cmd hup_cmd_items[]={{"sink",sink},{"many",many},{"ver",version}};
static const size_t hup_cmd_count=ARRAY_SIZE(hup_cmd_items);
static struct hup_resp hup_resp_items[1];
static const size_t hup_resp_count=0;
#include "../../../lib/hu/hupacket.c"

static int sends,commands,last_argc,last_send,send_error;
static size_t last_payload,reply_length;
static char reply[CONFIG_HU_PACKET_SIZE];
static int user_tag,enduser_tag;
static struct {
    uint64_t before;
    struct hup_handle h;
    uint64_t after;
} guarded;

void *palloc(size_t size) { return malloc(size); }
void pfree(void *p) { free(p); }
char *itoa(int value,char *out,int base) {
    if(base==10)sprintf(out,"%d",value);
    else { assert(base==16);sprintf(out,"%x",(unsigned)value); }
    return out;
}
static ssize_t sender(void *user,const uint8_t *data,size_t size) {
    assert(user==&user_tag && size<sizeof(reply));
    memcpy(reply,data,size);reply[size]=0;reply_length=size;sends++;
    return send_error ? send_error : (ssize_t)size;
}
static void check_guards(void) {
    assert(guarded.before==UINT64_C(0x1122334455667788));
    assert(guarded.after==UINT64_C(0x8877665544332211));
    assert(guarded.h.send==sender && guarded.h.user_data==&user_tag);
    assert(guarded.h.enduser_data==&enduser_tag);
}
static void sink(void *p,int argc,const char *argv[]) {
    commands++;last_argc=argc;assert(argc==2);last_payload=strlen(argv[1]);
    hupacket_ack_response(p,NULL);last_send=hupacket_send_buffer(p,NULL);
}
static void many(void *p,int argc,const char *argv[]) {
    commands++;last_argc=argc;
    for(int i=1;i<argc;i++)assert(!strcmp(argv[i],"v"));
    hupacket_ack_response(p,NULL);last_send=hupacket_send_buffer(p,NULL);
}
static void version(void *p,int argc,const char *argv[]) {
    commands++;hupacket_ack_response(p,NULL);
    hupacket_record_str(p,NULL,"version text deliberately longer than the remaining response space");
    last_send=hupacket_send_buffer(p,NULL);
}
static void feed(const uint8_t *data,size_t size) {
    struct hup_handle *h=&guarded.h;
    /* Exercise incremental reception, including splits immediately before EOT. */
    for(size_t i=0;i<size;i++)process_hupacket(h,(uint8_t *)data+i,1);
    assert(h->state==HUP_STATE_NONE && h->argc==0 && h->argv[0]==NULL);
    check_guards();
}
static void check_reply_crc(bool with_crc) {
    assert(reply_length>0 && reply[reply_length-1]==4);
    char *mark=strchr(reply,26);
    if(!with_crc) { assert(!mark);return; }
    assert(mark);
    uint16_t expected=crc16(CRC16_CCITT_POLY,0,(uint8_t *)reply+1,mark-reply-1);
    assert(strtoul(mark+1,NULL,16)==expected);
}
static void expect_size_nak(bool with_crc) {
    assert(reply[0]==21);
    char *record=strchr(reply,30);assert(record && strtol(record+1,NULL,10)==-EMSGSIZE);
    check_reply_crc(with_crc);
}
static void setup_tx(bool with_crc) {
    struct hup_handle *h=&guarded.h;
    reset_hupacket(h);h->argc=1;h->argv[0]="sink";h->id="19";h->sequence="7";
    h->crc16=with_crc ? "present" : NULL;
    hupacket_ack_response(h,NULL);
}
static void rx_boundaries(void) {
    const size_t cap=CONFIG_HU_PACKET_SIZE;
    uint8_t packet[CONFIG_HU_PACKET_SIZE+16];
    for(int with_crc=0;with_crc<2;with_crc++) {
        size_t body_size=cap-1-(with_crc ? 5 : 0);
        packet[0]=5;memcpy(packet+1,"sink\36",5);
        memset(packet+6,'x',body_size-5);
        size_t n=1+body_size;
        if(with_crc) {
            uint16_t crc=crc16(CRC16_CCITT_POLY,0,packet+1,body_size);
            packet[n++]=26;snprintf((char *)packet+n,5,"%04x",crc);n+=4;
        }
        packet[n++]=4;int before=commands;feed(packet,n);
        assert(commands==before+1 && last_payload==body_size-5 && last_send>0);
        check_reply_crc(with_crc);
    }
    packet[0]=5;memset(packet+1,'x',cap);packet[cap+1]=4;
    int before=commands;int sent=sends;feed(packet,cap+2);
    assert(commands==before && sends==sent);
    for(int args=64;args<=65;args++) {
        size_t n=0;packet[n++]=5;memcpy(packet+n,"many",4);n+=4;
        for(int i=1;i<args;i++) { packet[n++]=30;packet[n++]='v'; }
        packet[n++]=4;before=commands;feed(packet,n);
        assert(commands==before+(args==64));if(args==64)assert(last_argc==64);
    }
    static const uint8_t recovery[]="\5sink\36ok\4";
    feed(recovery,sizeof(recovery)-1);assert(last_payload==2 && last_send>0);
    puts("PASS: exact RX capacity with/without CRC, overflow discard, 64/65 arguments, bytewise recovery");
}
static void tx_boundaries(void) {
    struct hup_handle *h=&guarded.h;
    char payload[CONFIG_HU_PACKET_SIZE+1];memset(payload,'x',sizeof(payload));
    for(int with_crc=0;with_crc<2;with_crc++) {
        setup_tx(with_crc);
        size_t available=sizeof(h->tx_buffer)-(with_crc ? 7 : 2)-strlen(h->tx_buffer);
        payload[available]=0;
        hupacket_append_str(h,h->tx_buffer,payload);assert(h->tx_error==0);
        assert(hupacket_send_buffer(h,NULL)>0);check_reply_crc(with_crc);check_guards();
        if(!with_crc)assert(reply_length==sizeof(h->tx_buffer)-1);
        int before=sends;assert(hupacket_send_buffer(h,NULL)==-EALREADY && sends==before);
        hupacket_record_int(h,NULL,123);assert(h->tx_error==-EALREADY);

        setup_tx(with_crc);hupacket_append_str(h,NULL,payload);
        hupacket_append_char(h,NULL,'!');assert(h->tx_error==-EMSGSIZE);
        size_t length=strlen(h->tx_buffer);hupacket_record_int(h,NULL,INT_MIN);
        assert(strlen(h->tx_buffer)==length);
        before=sends;assert(hupacket_send_buffer(h,NULL)==-EMSGSIZE && sends==before+1);
        expect_size_nak(with_crc);check_guards();

        setup_tx(with_crc);memset(payload,'x',sizeof(payload));
        hupacket_append_str(h,NULL,payload);assert(h->tx_error==-EMSGSIZE);
        assert(hupacket_send_buffer(h,NULL)==-EMSGSIZE);expect_size_nak(with_crc);
    }
    setup_tx(true);hupacket_record_int(h,NULL,INT_MIN);hupacket_record_hex(h,NULL,UINT32_MAX);
    uint8_t bytes[]={0x00,0x04,0x05,0x1a,0x1e,0x80,0xff};
    hupacket_record_str(h,NULL,"");hupacket_append_hex_bytes(h,NULL,bytes,sizeof(bytes));
    assert(hupacket_send_buffer(h,NULL)>0);
    assert(strstr(reply,"-2147483648\36ffffffff\36" "0004051a1e80ff"));check_reply_crc(true);
    setup_tx(false);hupacket_append_hex_bytes(h,NULL,bytes,SIZE_MAX);
    assert(h->tx_error==-EMSGSIZE && hupacket_send_buffer(h,NULL)==-EMSGSIZE);
    expect_size_nak(false);

    char external[8]="guard";setup_tx(false);
    hupacket_append_str(h,external,"x");assert(h->tx_error==-EINVAL && !strcmp(external,"guard"));
    int before=sends;assert(hupacket_send_buffer(h,external)==-EINVAL && sends==before);
    hupacket_ack_response(h,external);assert(h->tx_error==-EINVAL && !strcmp(external,"guard"));
    setup_tx(true);memset(h->tx_buffer,'x',sizeof(h->tx_buffer));
    assert(hupacket_send_buffer(h,NULL)==-EMSGSIZE);expect_size_nak(true);
    setup_tx(false);send_error=-EIO;assert(hupacket_send_buffer(h,NULL)==-EIO);send_error=0;
    setup_tx(false);assert(hupacket_send_buffer(h,NULL)>0);check_guards();
    puts("PASS: TX capacity/trailer, overflow NAK, unterminated input, hex bytes, external buffer rejection, reset");
}
static void long_headers(void) {
    uint8_t packet[CONFIG_HU_PACKET_SIZE+16];
    for(int with_crc=0;with_crc<2;with_crc++) {
        size_t body_size=CONFIG_HU_PACKET_SIZE-24;
        packet[0]=5;memcpy(packet+1,"19@ver:",7);memset(packet+8,'9',body_size-7);
        size_t n=body_size+1;
        if(with_crc) {
            uint16_t crc=crc16(CRC16_CCITT_POLY,0,packet+1,body_size);
            packet[n++]=26;snprintf((char *)packet+n,5,"%04x",crc);n+=4;
        }
        packet[n++]=4;int before=sends;feed(packet,n);
        assert(last_send==-EMSGSIZE && sends==before+1);expect_size_nak(with_crc);
        assert(!memcmp(reply+1,packet+1,body_size));
    }
    packet[0]=5;memcpy(packet+1,"ver:",4);
    memset(packet+5,'9',CONFIG_HU_PACKET_SIZE-5);packet[CONFIG_HU_PACKET_SIZE]=4;
    int before=sends;feed(packet,CONFIG_HU_PACKET_SIZE+1);
    assert(last_send==-EMSGSIZE && sends==before);
    /* An oversized unknown command and CRC-error response must also be safe. */
    memset(packet+1,'x',CONFIG_HU_PACKET_SIZE-1);feed(packet,CONFIG_HU_PACKET_SIZE+1);
    assert(sends==before);
    size_t body_size=CONFIG_HU_PACKET_SIZE-6;
    packet[0]=5;memcpy(packet+1,"ver:",4);memset(packet+5,'9',body_size-4);
    uint16_t crc=crc16(CRC16_CCITT_POLY,0,packet+1,body_size)^1;
    size_t n=body_size+1;packet[n++]=26;snprintf((char *)packet+n,5,"%04x",crc);n+=4;packet[n++]=4;
    feed(packet,n);assert(sends==before);
    static const uint8_t recovery[]="\5ver:7\4";
    feed(recovery,sizeof(recovery)-1);assert(sends==before+1 && last_send>0);
    puts("PASS: long sequence response overflow, preserved NAK header, unrepresentable header dropped, recovery");
}
int main(void) {
    guarded.before=UINT64_C(0x1122334455667788);guarded.after=UINT64_C(0x8877665544332211);
    assert(init_hupacket(&guarded.h,sender,&user_tag)==&guarded.h);
    guarded.h.enduser_data=&enduser_tag;
    rx_boundaries();tx_boundaries();long_headers();check_guards();return 0;
}
