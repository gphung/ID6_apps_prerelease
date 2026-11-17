#include "gauge_core.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static void check(unsigned did, const unsigned char *p, size_t len, double expected) {
    double x=0; assert(bd_decode_did(did,p,len,&x)); assert(fabs(x-expected)<.00001);
}
static int parse_field(unsigned field, const char *replacement, bd_frame *out) {
    const char *tokens[]={"NBG2","1","0","31","1","18","0","14.7","0","-5","0","40","0","90","0"};
    char packet[BD_MAX_PACKET];
    size_t used=0;
    unsigned i;
    assert(field < sizeof tokens / sizeof tokens[0]);
    tokens[field]=replacement;
    for (i=0;i<sizeof tokens / sizeof tokens[0];++i) {
        int n=snprintf(packet+used,sizeof packet-used,"%s%s",i ? " " : "",tokens[i]);
        assert(n>0 && (size_t)n<sizeof packet-used);
        used+=(size_t)n;
    }
    return bd_parse(packet,used,out);
}
static void test_parser_boundaries(void) {
    const unsigned unsigned_fields[]={1,2,3,4,6,8,10,12,14};
    const char *bad_unsigned[]={"-1","+1","-4294967296","18446744073709551616","99999999999999999999999999999999999","1x",""};
    bd_frame out;
    unsigned i,j;
    for (i=0;i<sizeof unsigned_fields / sizeof unsigned_fields[0];++i)
        for (j=0;j<sizeof bad_unsigned / sizeof bad_unsigned[0];++j)
            assert(!parse_field(unsigned_fields[i],bad_unsigned[j],&out));
    assert(!parse_field(2,"-4294967296",&out));
    assert(!parse_field(3,"-4294967265",&out));
    assert(!parse_field(3,"4294967327",&out));
    assert(parse_field(1,"4294967295",&out) && out.seq==UINT32_MAX);
    assert(!parse_field(1,"4294967296",&out));
    assert(parse_field(2,"1",&out) && out.demo==1);
    assert(!parse_field(2,"2",&out));
    assert(parse_field(3,"31",&out) && out.valid==31);
    assert(!parse_field(3,"32",&out));
    assert(parse_field(4,"18446744073709551615",&out) && out.sent_ms==UINT64_MAX);
    for (i=6;i<=14;i+=2) {
        assert(parse_field(i,"999999",&out));
        assert(!parse_field(i,"1000000",&out));
    }
    assert(parse_field(1,"\t 123\r\n",&out) && out.seq==123);
    assert(!parse_field(1,"1 2",&out));
    assert(!parse_field(5,"inf",&out));
    assert(!parse_field(5,"1e999",&out));
    assert(!parse_field(5,"10000.1",&out));
    assert(parse_field(5,"-10000",&out) && out.value[0]==-10000);
    { const char embedded_nul[]="NBG2 1 0 31 1 18 0 14 0 0 0 40 0 90 0\0junk";
      assert(!bd_parse(embedded_nul,sizeof embedded_nul-1,&out)); }
}
int main(void) {
    /* Actual payload examples in OBDb's 2016 3-Series captures (CC BY-SA 4.0). */
    const unsigned char baro[]={0x63,0xca}, pressure[]={0x31,0xe5}, timing[]={0,0x4e};
    const unsigned char iat[]={0x82}, lambda[]={0x10,0x4e}, trans[]={0,0x8e};
    const unsigned char negative_timing[]={0xff,0xce}, invalid[]={0xff,0xff};
    double x;
    bd_frame f, g;
    char packet[BD_MAX_PACKET];
    int n;
    check(0x4201,baro,2,99.7890625); check(0x4205,pressure,2,99.7890625);
    check(0x4a49,timing,2,7.8); check(0x4a49,negative_timing,2,-5);
    { const unsigned char near_zero[]={0xff,0xfe}, signed_min[]={0x80,0};
      check(0x4a49,near_zero,2,-.2);
      assert(!bd_decode_did(0x4a49,invalid,2,&x));
      assert(!bd_decode_did(0x4a49,signed_min,2,&x)); }
    check(0x580f,iat,1,49.5); check(0x5889,lambda,2,4174*16.0/65535);
    check(0xda12,trans,2,94);
    assert(!bd_decode_did(0x4205,baro,1,&x)); assert(!bd_decode_did(0xda12,invalid,2,&x));
    assert(!bd_decode_did(0x5889,invalid,2,&x)); assert(!bd_decode_did(0xffff,baro,2,&x));
    { const unsigned dids[]={0x4201,0x4205,0x5889,0x4a49,0x580f,0xda12};
      const unsigned char *payloads[]={baro,pressure,lambda,timing,iat,trans};
      unsigned i;
      for (i=0;i<sizeof dids / sizeof dids[0];++i) {
          unsigned char extra[3]={0,0,0xaa};
          size_t width=dids[i]==0x580f ? 1u : 2u;
          memcpy(extra,payloads[i],width);
          x=123;
          assert(!bd_decode_did(dids[i],payloads[i],width-1,&x) && x==123);
          assert(!bd_decode_did(dids[i],extra,width+1,&x) && x==123);
      } }
    { /* F87 M2 N55, ignition on, engine off, 2026-11-16 (de-identified). */
      const unsigned char obd_coolant[]={0x30}, did_coolant_one_byte[]={0x4b};
      const unsigned char oil[]={0x04,0x17}, lambda_engine_off[]={0xff,0xff};
      const unsigned char ambient[]={0x65,0x2e}, manifold[]={0x32,0x97};
      double c=0;
      assert(bd_decode_pid(0x05,obd_coolant,1,&c) && fabs(c-8)<.00001);
      x=123; assert(!bd_decode_pid(0x05,obd_coolant,0,&x) && x==123);
      assert(!bd_decode_pid(0x05,oil,2,&x) && x==123);
      { const unsigned char hot[]={190}, too_hot[]={191};
        assert(bd_decode_pid(0x05,hot,1,&c) && fabs(c-150)<.00001);
        assert(!bd_decode_pid(0x05,too_hot,1,&x)); }
      assert(!bd_decode_pid(0x0c,obd_coolant,1,&x));
      assert(!bd_decode_did(0x4300,did_coolant_one_byte,1,&x) && x==123);
      check(0x586f,oil,2,1.047);
      assert(!bd_decode_did(0x5889,lambda_engine_off,2,&x));
      check(0x4201,ambient,2,25902/256.0); check(0x4205,manifold,2,12951/128.0); }
    { /* N55 oil temperature (DME 4408) and warm-up bands: 180 F warming, 194 F ready,
         266 F hot, each held 1 C below its threshold on the way down. */
      const unsigned char oil_cold[]={0x0a,0xfc}, oil_absent[]={0xff,0xff};
      const double warming=(180.0-32)/1.8, ready=(194.0-32)/1.8, hot=(266.0-32)/1.8;
      check(0x4408,oil_cold,2,281.2-273.15);
      x=123; assert(!bd_decode_did(0x4408,oil_absent,2,&x) && x==123);
      assert(!bd_decode_did(0x4408,oil_cold,1,&x));
      assert(bd_oil_band(20,-1)==BD_OIL_COLD);
      assert(bd_oil_band(warming-.01,-1)==BD_OIL_COLD);
      assert(bd_oil_band(warming,-1)==BD_OIL_WARMING);
      assert(bd_oil_band(ready,BD_OIL_WARMING)==BD_OIL_READY);
      assert(bd_oil_band(hot,BD_OIL_READY)==BD_OIL_HOT);
      assert(bd_oil_band(ready-.5,BD_OIL_READY)==BD_OIL_READY);   /* inside the buffer */
      assert(bd_oil_band(ready-1.01,BD_OIL_READY)==BD_OIL_WARMING);
      assert(bd_oil_band(ready-.5,BD_OIL_WARMING)==BD_OIL_WARMING); /* no jump up early */
      assert(bd_oil_band(warming-.5,BD_OIL_WARMING)==BD_OIL_WARMING);
      assert(bd_oil_band(warming-1.01,BD_OIL_WARMING)==BD_OIL_COLD);
      assert(bd_oil_band(hot-.5,BD_OIL_HOT)==BD_OIL_HOT);
      assert(bd_oil_band(100,BD_OIL_HOT)==BD_OIL_READY);
      assert(bd_oil_band(60,BD_OIL_HOT)==BD_OIL_COLD);
      assert(bd_oil_band(150,-1)==BD_OIL_HOT);
      assert(bd_oil_band(NAN,BD_OIL_READY)==-1);
      assert(fabs(warming-82.2222)<.001 && fabs(ready-90)<1e-9 && fabs(hot-130)<1e-9); }
    test_parser_boundaries();
    memset(&f,0,sizeof f); f.seq=123; f.valid=31; f.sent_ms=1234567890123ULL;
    f.value[0]=18.25; f.value[1]=14.7;
    f.value[2]=-5; f.value[3]=49.5; f.value[4]=94;
    n=bd_encode(&f,packet,sizeof packet); assert(n>0); assert(bd_parse(packet,(size_t)n,&g));
    assert(g.seq==123 && g.valid==31 && fabs(g.value[0]-18.25)<.0001);
    assert(g.sent_ms==1234567890123ULL);
    assert(bd_is_fresh(&g,0,2499)); assert(!bd_is_fresh(&g,0,2500));
    g.age_ms[0]=2000; assert(bd_is_fresh(&g,0,499)); assert(!bd_is_fresh(&g,0,500));
    g.valid=0; assert(!bd_is_fresh(&g,0,0)); assert(!bd_is_fresh(&g,9,0));
    assert(!bd_parse("NBG2",4,&g)); assert(!bd_parse(packet,BD_MAX_PACKET,&g));
    { const char *bad="NBG2 1 0 31 1 nan 0 14 0 0 0 40 0 90 0";
      assert(!bd_parse(bad,strlen(bad),&g)); }
    packet[n-1]='x'; assert(!bd_parse(packet,(size_t)n,&g));
    assert(fabs(bd_display_value(BD_BOOST,14.5037738,1)-1)<.0001);
    assert(fabs(bd_display_value(BD_IAT,100,0)-212)<.0001);
    assert(bd_bar(BD_BOOST,0)==0 && bd_bar(BD_BOOST,-100)==0 && bd_bar(BD_BOOST,999)==1000);
    puts("Core: recorded signal payloads, signed timing, sentinels, wire format and stale data passed.");
    return 0;
}
