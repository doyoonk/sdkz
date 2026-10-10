/*
 * Copyright (c) 2025 HU Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hu/hupacket.h>
#include <zephyr/kernel.h>
#include <stdio.h>

static void ping(void *,int,const char *[]);
static void response(void *,int,const char *[],void *);
static void _flash(void *,int,const char **);
static void _erase(void *,int,const char **);
static void _wrprt(void *,int,const char **);
static struct hup_cmd hup_cmd_items[]={
    {"ping",ping},{"flash",_flash},{"erase",_erase},{"wrprt",_wrprt},
};
static const size_t hup_cmd_count=ARRAY_SIZE(hup_cmd_items);
static struct hup_resp hup_resp_items[]={{"ping",response}};
static const size_t hup_resp_count=ARRAY_SIZE(hup_resp_items);

#include "../../../lib/hu/hupacket.c"
#include "../../../lib/hu/ascii85.c"
#include "../../../lib/hu/huflash.c"

static int sends,commands,responses,writes,erases,opens,closes,last_id;
static int open_error,write_error,erase_error,slot_error;
static uint8_t active_slot;
static bool last_response_crc;
static char last_reply[CONFIG_HU_PACKET_SIZE];
const struct device test_internal_flash={1}, test_external_flash={2};
static struct flash_area areas[]={
    {2,0,4096,&test_external_flash}, {3,4096,4096,&test_external_flash}
};
static uint8_t flash_bytes[4096];

void *palloc(size_t size) { return malloc(size); }
void pfree(void *p) { free(p); }
char *itoa(int value,char *str,int base) {
    if(base==10)sprintf(str,"%d",value);
    else { assert(base==16);sprintf(str,"%x",(unsigned)value); }
    return str;
}
int bootloader_active_slot(uint8_t *slot) { *slot=active_slot;return slot_error; }
int flash_area_open(uint8_t id,const struct flash_area **fa) {
    if(open_error)return open_error;
    assert(id==2 || id==3);*fa=&areas[id-2];last_id=id;opens++;return 0;
}
void flash_area_close(const struct flash_area *fa) { assert(fa);closes++; }
int flash_area_write(const struct flash_area *fa,off_t off,const void *src,size_t len) {
    writes++;assert(off>=0 && (size_t)off+len<=fa->fa_size);
    if(write_error)return write_error;
    memcpy(flash_bytes+off,src,len);return 0;
}
int flash_area_erase(const struct flash_area *fa,off_t off,size_t len) {
    erases++;assert(off==0 && len==fa->fa_size);return erase_error;
}
static ssize_t sender(void *user,const uint8_t *bytes,size_t n) {
    assert(n<sizeof(last_reply));memcpy(last_reply,bytes,n);last_reply[n]=0;sends++;return n;
}
static void ping(void *h,int argc,const char *argv[]) {
    assert(argc==2);commands++;
    hupacket_ack_response(h,NULL);hupacket_record_str(h,NULL,argv[1]);hupacket_send_buffer(h,NULL);
}
static void response(void *h,int argc,const char *argv[],void *data) {
    assert(argc==2 && !strcmp(argv[1],"0"));responses++;
    last_response_crc=((struct hup_handle *)h)->crc_match;
}
static uint16_t body_crc(const char *body) {
    return crc16(CRC16_CCITT_POLY,0,(const uint8_t *)body,strlen(body));
}
static void idle(struct hup_handle *h) {
    assert(h->state==HUP_STATE_NONE && h->argc==0 && !h->crc16 && !h->id && !h->sequence);
    assert(!h->response && !h->crc_match);
}
static size_t make_packet(char *out,unsigned char start,const char *body,const char *crc) {
    int n=crc ? snprintf(out,2048,"%c%s\32%s\4",start,body,crc)
              : snprintf(out,2048,"%c%s\4",start,body);
    assert(n>0 && n<2048);return n;
}
static void feed(struct hup_handle *h,unsigned char start,const char *body,const char *crc) {
    char packet[2048];size_t n=make_packet(packet,start,body,crc);
    process_hupacket(h,(uint8_t *)packet,n);idle(h);
}
static void valid(struct hup_handle *h,unsigned char start,const char *body) {
    char crc[5];snprintf(crc,sizeof(crc),"%x",body_crc(body));feed(h,start,body,crc);
}
static void expected_reply(unsigned char start,const char *body) {
    char expected[2048],crc[5];snprintf(crc,sizeof(crc),"%x",body_crc(body));
    make_packet(expected,start,body,crc);assert(!strcmp(last_reply,expected));
}
static void status(int rc) {
    assert(last_reply[0]==(rc ? 21 : 6));
    char *record=strchr(last_reply,30),*crc=strchr(last_reply,26);
    assert(record && crc && strtol(record+1,NULL,10)==rc);
    uint16_t actual=crc16(CRC16_CCITT_POLY,0,(uint8_t *)last_reply+1,crc-last_reply-1);
    assert(strtoul(crc+1,NULL,16)==actual);
    assert(opens==closes);
}
static void flash_request(struct hup_handle *h,const char *partition,const char *offset,
                          const char *length,const char *encoded,int rc) {
    char body[1024];snprintf(body,sizeof(body),"19@flash:7\36%s\36%s\36%s\36%s",
        partition,offset,length,encoded);
    int before=sends;valid(h,5,body);assert(sends==before+1);status(rc);
}

#if defined(CONFIG_FLASH_STM32_WRITE_PROTECT) && defined(CONFIG_FLASH_EX_OP_ENABLED) && \
    defined(CONFIG_FLASH_PAGE_LAYOUT)
static int wp_reads, wp_writes, page_error, wp_read_error, wp_write_error;
static uint64_t protected_mask, last_enable, last_disable;
static bool ignore_wp;
static uint32_t page_index_bias;
int flash_get_page_info_by_offs(const struct device *dev, off_t offset,
                              struct flash_pages_info *info) {
    assert(dev==&test_internal_flash);
    if(page_error)return page_error;
    if(offset<0 || offset>=8192)return -EINVAL;
    /* Nonuniform sectors: 1K, 1K, 2K, 4K. */
    off_t start=offset<1024 ? 0 : offset<2048 ? 1024 : offset<4096 ? 2048 : 4096;
    *info=(struct flash_pages_info){start,start<2048 ? 1024 : start==2048 ? 2048 : 4096,
        (start<2048 ? start/1024 : start==2048 ? 2 : 3)+page_index_bias};
    return 0;
}
int flash_ex_op(const struct device *dev,uint16_t code,uintptr_t in,void *out) {
    assert(dev==&test_internal_flash && code==FLASH_STM32_EX_OP_SECTOR_WP && out);
    if(in) {
        const struct flash_stm32_ex_op_sector_wp_in *request=(const void *)in;
        wp_writes++;last_enable=request->enable_mask;last_disable=request->disable_mask;
        if(wp_write_error)return wp_write_error;
        if(!ignore_wp)protected_mask=(protected_mask&~last_disable)|last_enable;
    } else { wp_reads++;if(wp_read_error)return wp_read_error; }
    ((struct flash_stm32_ex_op_sector_wp_out *)out)->protected_mask=protected_mask;
    return 0;
}
static void test_write_protection(struct hup_handle *h) {
    areas[0].fa_dev=areas[1].fa_dev=&test_internal_flash;
    protected_mask=UINT64_C(1)<<12;
    valid(h,5,"19@wrprt:7\36slot0\36" "1");status(0);
    assert(last_enable==7 && last_disable==0 && protected_mask==((UINT64_C(1)<<12)|7));
    int before=wp_writes;
    valid(h,5,"19@wrprt:7\36slot0\36" "1");status(0);assert(wp_writes==before);
    active_slot=0;valid(h,5,"19@wrprt:7\36inactive\36" "1");status(0);
    assert(last_enable==8 && protected_mask==((UINT64_C(1)<<12)|15));
#if defined(CONFIG_FLASH_STM32_WRITE_PROTECT_DISABLE_PREVENTION)
    before=wp_writes;
    valid(h,5,"19@wrprt:7\36slot0\36" "0");status(-EACCES);assert(wp_writes==before);
#else
    valid(h,5,"19@wrprt:7\36slot0\36" "0");status(0);
    assert(last_enable==0 && last_disable==7 && protected_mask==((UINT64_C(1)<<12)|8));
#endif
    before=wp_writes;
    areas[0].fa_off=1;areas[0].fa_size=4095;
    valid(h,5,"19@wrprt:7\36slot0\36" "1");status(-EINVAL);
    areas[0].fa_off=0;areas[0].fa_size=4095;
    valid(h,5,"19@wrprt:7\36slot0\36" "1");status(-EINVAL);
    areas[0].fa_size=0;valid(h,5,"19@wrprt:7\36slot0\36" "1");status(-EINVAL);
    areas[0].fa_off=INT32_MAX;areas[0].fa_size=4096;
    valid(h,5,"19@wrprt:7\36slot0\36" "1");status(-EINVAL);
    areas[0].fa_off=0;page_index_bias=64;
    valid(h,5,"19@wrprt:7\36slot0\36" "1");status(-ENOTSUP);page_index_bias=0;
    page_error=-EIO;valid(h,5,"19@wrprt:7\36slot0\36" "1");status(-EIO);page_error=0;
    wp_read_error=-EIO;valid(h,5,"19@wrprt:7\36slot0\36" "1");status(-EIO);wp_read_error=0;
    assert(wp_writes==before);
    protected_mask=0;wp_write_error=-EIO;
    valid(h,5,"19@wrprt:7\36slot0\36" "1");status(-EIO);wp_write_error=0;
    ignore_wp=true;valid(h,5,"19@wrprt:7\36slot0\36" "1");status(-EIO);ignore_wp=false;
    /* Highest representable sector must not overflow the mask. */
    page_index_bias=60;valid(h,5,"19@wrprt:7\36slot1\36" "1");status(0);
    assert(last_enable==(UINT64_C(1)<<63));page_index_bias=0;
    before=wp_reads+wp_writes;areas[0].fa_dev=&test_external_flash;
    valid(h,5,"19@wrprt:7\36slot0\36" "1");status(-ENOTSUP);
    assert(wp_reads+wp_writes==before);
    puts("PASS: partition WP masks/readback, no-op, alignment, range, driver errors, device isolation");
}
#endif

int main(void) {
    struct hup_handle *h=init_hupacket(NULL,sender,NULL);assert(h);
    const char *body="19@ping:7\36hello";
    valid(h,5,body);assert(commands==1 && sends==1);
    expected_reply(6,"19@ping:7\36" "0\36hello");
    char bad_crc[5];snprintf(bad_crc,sizeof(bad_crc),"%x",body_crc(body)^1);
    feed(h,5,body,bad_crc);assert(commands==1 && sends==2);
    char error_body[80];snprintf(error_body,sizeof(error_body),"19@ping:7\36%d",-ENMCRC16);
    expected_reply(21,error_body);

    const char *bad[]={"","-1","+1","0x1"," 1","10000","fffff","x","1 ","1\36x","1\32x"};
    for(size_t i=0;i<ARRAY_SIZE(bad);i++) {
        int before=sends;feed(h,5,body,bad[i]);assert(sends==before+1 && commands==1);
        expected_reply(21,error_body);
    }
    char suffix[16];snprintf(suffix,sizeof(suffix),"%xjunk",body_crc(body));
    feed(h,5,body,suffix);assert(commands==1);expected_reply(21,error_body);
    char embedded[2048],crc[5];snprintf(crc,sizeof(crc),"%x",body_crc(body));
    size_t n=make_packet(embedded,5,body,crc);
    embedded[n-1]=0;embedded[n]='x';embedded[n+1]=4;
    process_hupacket(h,(uint8_t *)embedded,n+2);idle(h);expected_reply(21,error_body);

    for(int start=6;start<=21;start+=15) {
        int before=sends;feed(h,start,"19@ping:7\36" "0","bad!");
        assert(responses==0 && sends==before);
        char mismatch[5];snprintf(mismatch,sizeof(mismatch),"%x",body_crc("19@ping:7\36" "0")^1);
        feed(h,start,"19@ping:7\36" "0",mismatch);
        assert(responses==0 && sends==before);
    }
    valid(h,6,"19@ping:7\36" "0");valid(h,21,"19@ping:7\36" "0");
    assert(responses==2 && last_response_crc);
    feed(h,6,"19@ping:7\36" "0",NULL);assert(responses==3 && !last_response_crc);
    char pair[4096],first[2048],second[2048];
    size_t one=make_packet(first,5,body,bad_crc),two=make_packet(second,5,body,NULL);
    memcpy(pair,first,one);memcpy(pair+one,second,two);
    int before=sends;process_hupacket(h,(uint8_t *)pair,one+two);idle(h);
    assert(commands==2 && sends==before+2);
    valid(h,5,body);valid(h,5,body);assert(commands==4);
    bool short_crc=false;
    for(unsigned i=0;i<65536;i++) {
        char small[64];snprintf(small,sizeof(small),"19@ping:7\36%u",i);
        if(body_crc(small)<16) { valid(h,5,small);short_crc=true;break; }
    }
    assert(short_crc);snprintf(crc,sizeof(crc),"%04X",body_crc(body));feed(h,5,body,crc);
    before=sends;
    uint8_t oversized[CONFIG_HU_PACKET_SIZE+2];
    oversized[0]=5;memset(oversized+1,'x',CONFIG_HU_PACKET_SIZE);oversized[sizeof(oversized)-1]=4;
    process_hupacket(h,oversized,sizeof(oversized));idle(h);assert(sends==before);
    uint8_t records[67];records[0]=5;memset(records+1,30,65);records[66]=4;
    process_hupacket(h,records,sizeof(records));idle(h);assert(sends==before);
    valid(h,5,body);assert(sends==before+1);
    puts("PASS: strict CRC, request NAK header/CRC, rejected bad responses, reset/recovery, repeated sequence");

    uint8_t data[256];char encoded[400],length[16];
    const size_t sizes[]={1,2,3,4,255,256};
    for(size_t j=0;j<ARRAY_SIZE(sizes);j++) {
        for(size_t i=0;i<sizes[j];i++)data[i]=(uint8_t)(i+1);
        int len=encode_ascii85(data,sizes[j],(uint8_t *)encoded,sizeof(encoded));assert(len>0);
        encoded[len]=0;snprintf(length,sizeof(length),"%zx",sizes[j]);
        flash_request(h,"slot0","0x100",length,encoded,0);assert(last_id==2);
        assert(!memcmp(flash_bytes+0x100,data,sizes[j]));
    }
    memset(data,0,sizeof(data));int len=encode_ascii85(data,sizeof(data),(uint8_t *)encoded,sizeof(encoded));
    encoded[len]=0;flash_request(h,"slot1","f00","100",encoded,0);assert(last_id==3);
    assert(!memcmp(flash_bytes+0xf00,data,sizeof(data)));
    int unchanged=writes;
    const char *bad_sizes[]={"0","101","-1","+1","1x"," 1","","100000000","0x"};
    for(size_t i=0;i<ARRAY_SIZE(bad_sizes);i++)flash_request(h,"slot0","0",bad_sizes[i],"z",-EINVAL);
    const char *bad_offsets[]={"-1","+1","100000000","80000000","fffffff0","1000","fff","0x","1g"};
    for(size_t i=0;i<ARRAY_SIZE(bad_offsets);i++)flash_request(h,"slot0",bad_offsets[i],"4","z",-EINVAL);
    flash_request(h,"slot0","0","1","z",-EDESZA85);
    flash_request(h,"slot0","0","4","",-EDESZA85);
    flash_request(h,"slot0","0","4","!",-ECODEA85);
    flash_request(h,"slot0","0","4","~~~~~",-(EASCII85+2));
    flash_request(h,"slot0","0","4","uuuuu",-(EASCII85+3));
    memset(encoded,'z',65);encoded[65]=0;
    flash_request(h,"slot0","0","100",encoded,-EDESZA85);
    flash_request(h,"unknown","0","4","z",-EINVAL);
    assert(writes==unchanged);
    write_error=-EIO;flash_request(h,"slot0","0","4","z",-EIO);write_error=0;
    open_error=-EACCES;flash_request(h,"slot0","0","4","z",-EACCES);open_error=0;
    puts("PASS: nonzero partition IDs, 1..256-byte chunks, ASCII85 groups/z, size/address checks, driver errors");

    active_slot=0;flash_request(h,"inactive","0","4","z",0);assert(last_id==3);
    active_slot=1;flash_request(h,"inactive","0","4","z",0);assert(last_id==2);
    active_slot=2;flash_request(h,"inactive","0","4","z",-EINVAL);
    slot_error=-EIO;flash_request(h,"inactive","0","4","z",-EIO);slot_error=0;
    valid(h,5,"19@erase:7\36slot0");status(0);assert(erases==1 && last_id==2);
    erase_error=-EIO;valid(h,5,"19@erase:7\36slot1");status(-EIO);erase_error=0;
    valid(h,5,"19@erase:7\36slot0\36extra");status(-EINVAL);assert(erases==2);
    unchanged=writes;
    valid(h,5,"19@wrprt:7\36slot0\36" "1");status(-ENOTSUP);
    valid(h,5,"19@wrprt:7\36slot1\36" "0");status(-ENOTSUP);
    valid(h,5,"19@wrprt:7\36slot0\36" "2");status(-EINVAL);
    valid(h,5,"19@wrprt:7\36slot0");status(-EINVAL);
    assert(writes==unchanged && erases==2);
    puts("PASS: inactive slot mapping/errors, erase status, unsupported wrprt always replies");
#if defined(CONFIG_FLASH_STM32_WRITE_PROTECT) && defined(CONFIG_FLASH_EX_OP_ENABLED) && \
    defined(CONFIG_FLASH_PAGE_LAYOUT)
    test_write_protection(h);
#endif
    deinit_hupacket(h);return 0;
}
