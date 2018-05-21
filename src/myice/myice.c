#include <pjsip.h>
#include <pjmedia.h>
#include <pjmedia-codec.h>
#include <pjsip_ua.h>
#include <pjsip_simple.h>
#include <pjlib-util.h>
#include <pjlib.h>
#include <pjmedia-codec/h264_packetizer.h>

#include <stdlib.h>

#define THIS_FILE "icetransport.c"
pj_caching_pool cp;
pjmedia_sdp_session * localSdp;
pjmedia_sdp_session * remoteSdp;
int iceState = 0; //1 for gathering ok. 2 for negotiation ok
char remoteSdpStr[2048]={0};

#define OFFER 1
#define ANSWER 2
#define OFFERSDP "offer.sdp"
#define ANSWERSDP "answer.sdp"
int gRole = OFFER;
char * gOpenFile = NULL;


void on_ice_complete2(pjmedia_transport *tp,
    pj_ice_strans_op op,
    pj_status_t status,
    void *user_data) {

    printf("------------------------------------------------------>on_ice_complete2\n");

}

void on_ice_complete(pjmedia_transport *tp,
    pj_ice_strans_op op,
    pj_status_t status) {

    switch (op) {
        /** Initialization (candidate gathering) */
    case PJ_ICE_STRANS_OP_INIT:
        iceState = 1;
        printf("--->gathering candidates finish\n");
        break;

        /** Negotiation */
    case PJ_ICE_STRANS_OP_NEGOTIATION:
        printf("--->PJ_ICE_STRANS_OP_NEGOTIATION\n");
        iceState = 2;
        break;

        /** This operation is used to report failure in keep-alive operation.
        *  Currently it is only used to report TURN Refresh failure.  */
    case PJ_ICE_STRANS_OP_KEEP_ALIVE:
        printf("--->PJ_ICE_STRANS_OP_KEEP_ALIVE\n");
        break;

        /** IP address change notification from STUN keep-alive operation.  */
    case PJ_ICE_STRANS_OP_ADDR_CHANGE:
        printf("--->PJ_ICE_STRANS_OP_ADDR_CHANGE\n");
        break;
    }
}

void write_sdp(char * buf, int len, char * fname){
    FILE * f = fopen(fname, "wb");
    assert(f != NULL);
    
    int wlen = fwrite(buf, 1, len, f);
    assert(wlen == len);
    
    fclose(f);
}
void input_confirm(char * pmt){
    char input[10];
    while(1){
        printf("%s, confirm(ok):", pmt);
        memset(input, 0, sizeof(input));
        scanf("%s", input);
        if(strcmp("ok", input) == 0){
            break;
        }
    }
}
void sdp_from_file(pjmedia_sdp_session ** sdp){
    input_confirm("input peer sdp file.(read from file)");
    
    //FILE * f = fopen("/Users/liuye/Documents/p2p/build/src/mysiprtp/Debug/r.sdp", "rb");
    FILE * f = fopen(gOpenFile, "rb");
    assert(f != NULL);
    
    fseek(f,0, SEEK_END);
    int flen = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    memset(remoteSdpStr, 0, sizeof(remoteSdpStr));
    int rlen = fread(remoteSdpStr, 1, flen, f);
    assert(rlen == flen);
    fclose(f);
    
    pj_pool_t * sdpRemotepool = pj_pool_create(&cp.factory, "sdpremote", 2048, 1024, NULL);
    
    pj_status_t status;
    status = pjmedia_sdp_parse(sdpRemotepool, remoteSdpStr, strlen(remoteSdpStr), sdp);
    assert(status == PJ_SUCCESS);
    
    char sdptxt[2048] = {0};
    pjmedia_sdp_print(*sdp, sdptxt, sizeof(sdptxt) - 1);
    printf("\n------------sdp from file start-----------\n");
    printf("%s", sdptxt);
    printf("\n------------sdp from file   end-----------\n");
}

void createOffer(pjmedia_endpt *endpt, pjmedia_transport *transport, pjmedia_sdp_session **p_sdp, pjmedia_transport_info *tinfo) {
    pj_str_t originStrAddr = pj_str("localhost");
    pj_sockaddr originAddr;
    pj_status_t status;
    status = pj_sockaddr_parse(pj_AF_INET(), 0, &originStrAddr, &originAddr);
    assert(status == PJ_SUCCESS);

    pj_pool_t * sdppool = pj_pool_create(&cp.factory, "sdppool", 2048, 1024, NULL);
    status = pjmedia_endpt_create_base_sdp(endpt, sdppool, NULL, &originAddr, p_sdp);
    assert(status == PJ_SUCCESS);

    pjmedia_sdp_media * sdpMedia;
    status = pjmedia_endpt_create_audio_sdp(endpt, sdppool, &tinfo->sock_info, 0, &sdpMedia);
    assert(status == PJ_SUCCESS);

    pjmedia_sdp_session *sdp = *p_sdp;
    sdp->media[sdp->media_count++] = sdpMedia;

    char sdpStr[2048];
    memset(sdpStr, 0, 2048);
    pjmedia_sdp_print(sdp, sdpStr, sizeof(sdpStr));
    printf("%s\n", sdpStr);

    status = pjmedia_transport_media_create(transport, sdppool, 0, NULL, 0);
    assert(status == PJ_SUCCESS);

    status = pjmedia_transport_encode_sdp(transport, sdppool, sdp, NULL, 0);
    assert(status == PJ_SUCCESS);

    memset(sdpStr, 0, sizeof(sdpStr));
    pjmedia_sdp_print(sdp, sdpStr, sizeof(sdpStr));
    printf("%s\n", sdpStr);
    write_sdp(sdpStr, strlen(sdpStr), OFFERSDP);
}

void createAnswer(pjmedia_endpt *endpt, pjmedia_transport *transport, pjmedia_sdp_session **p_sdp, pjmedia_sdp_session *offer, pjmedia_transport_info *tinfo) {
    pj_str_t originStrAddr = pj_str("localhost");
    pj_sockaddr originAddr;
    pj_status_t status;
    status = pj_sockaddr_parse(pj_AF_INET(), 0, &originStrAddr, &originAddr);
    assert(status == PJ_SUCCESS);
    
    pj_pool_t * sdppool = pj_pool_create(&cp.factory, "sdppool", 2048, 1024, NULL);
    status = pjmedia_endpt_create_base_sdp(endpt, sdppool, NULL, &originAddr, p_sdp);
    assert(status == PJ_SUCCESS);
    
    pjmedia_sdp_media * sdpMedia;
    status = pjmedia_endpt_create_audio_sdp(endpt, sdppool, &tinfo->sock_info, 0, &sdpMedia);
    assert(status == PJ_SUCCESS);
    
    pjmedia_sdp_session *sdp = *p_sdp;
    sdp->media[sdp->media_count++] = sdpMedia;
    
    char sdpStr[2048];
    memset(sdpStr, 0, sizeof(sdpStr));
    pjmedia_sdp_print(sdp, sdpStr, sizeof(sdpStr));
    printf("%s\n", sdpStr);
    
    status = pjmedia_transport_media_create(transport, sdppool, 0, offer, 0);
    assert(status == PJ_SUCCESS);
    
    status = pjmedia_transport_encode_sdp(transport, sdppool, sdp, offer, 0);
    assert(status == PJ_SUCCESS);
    
    memset(sdpStr, 0, sizeof(sdpStr));
    pjmedia_sdp_print(sdp, sdpStr, sizeof(sdpStr));
    printf("%s\n", sdpStr);
    write_sdp(sdpStr, strlen(sdpStr), ANSWERSDP);
}

void setLocalDescription(pjmedia_sdp_session *sdp) {
    localSdp = sdp;
}

void setRemoteDescription(pjmedia_sdp_session *sdp) {
    remoteSdp = sdp;
}

static int my_ice_thread(void *arg){
    pj_ioqueue_t * ioqueue = (pj_ioqueue_t *)arg;
    while(1) {
        const pj_time_val delay = {.sec = 0, .msec = 10};
        pj_ioqueue_poll(ioqueue, &delay);
    }
}

static int my_timer_func(void* arg) {
    pj_timer_heap_t *ht = (pj_timer_heap_t *)arg;
    while(1) {
        pj_timer_heap_poll(ht, NULL);
        pj_thread_sleep(100);
    }
    return 0;
}

int main(int argc, char **argv) {
    pj_status_t status;
    status = pj_init();
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);
    status = pjlib_util_init();
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);

    pj_caching_pool_init(&cp, &pj_pool_factory_default_policy, 0);

    //pj_log_set_level(1);
    
    pjmedia_endpt *med_endpt;
    status = pjmedia_endpt_create(&cp.factory, NULL, 1, &med_endpt);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);

  
    int roleOk = 0;
    do{
        printf("select role:\n1 for offer\n2 for answer\n:");
        scanf("%d", &gRole);
        if(gRole == OFFER || gRole == ANSWER){
            roleOk = 1;
        }else{
            printf("input is wrong");
        }
    }while(roleOk == 0);

    /* start ice config start */
    pj_timer_heap_t *ht = NULL;
    pj_pool_t * timerpool = pj_pool_create(&cp.factory, "timerpool", 2048, 1024, NULL);
    pj_timer_heap_create(timerpool, 100, &ht);

    //pj_ice_strans_turn_cfg turn_tp[PJ_ICE_MAX_TURN];
    pj_ice_strans_cfg g_icecfg;
    pj_ice_strans_cfg_default(&g_icecfg);
    g_icecfg.af = pj_AF_INET();
    //stun turn deprecated
    pj_bzero(&g_icecfg.stun, sizeof(g_icecfg.stun));
    pj_bzero(&g_icecfg.turn, sizeof(g_icecfg.turn));

    //这里按照注释掉的这样写会获取host地址
    //g_icecfg.stun_tp_cnt = 1;
    //pj_ice_strans_stun_cfg_default(&g_icecfg.stun_tp[0]);
    //需要获取stun地址还需要配置g_icecfg.stun_tp[0]的stun服务器地址和端口
#define NOT_USE_MEPTQ
#ifdef NOT_USE_MEPTQ
    pj_ioqueue_t* ioqueue;
    pj_pool_t * ioqpool = pj_pool_create(&cp.factory, "ioqpool", 2048, 1024, NULL);
    pj_ioqueue_create(ioqpool, 16, &ioqueue);
    pj_stun_config_init(&g_icecfg.stun_cfg, &cp.factory, 0,
                        ioqueue, ht);
    pj_thread_t*         pThread;
    pj_thread_create(ioqpool, NULL, &my_ice_thread, ioqueue, 0, 0, &pThread);
    
    pj_thread_t*         thThread;
    pj_pool_t * thpool = pj_pool_create(&cp.factory, "thpool", 2048, 1024, NULL);
    pj_thread_create(thpool, NULL, &my_timer_func, ht, 0, 0, &thThread);
#else
    pj_stun_config_init(&g_icecfg.stun_cfg, &cp.factory, 0,
        pjmedia_endpt_get_ioqueue(med_endpt), ht);
#endif
    


    g_icecfg.turn_tp_cnt = 1;
    pj_ice_strans_turn_cfg_default(&g_icecfg.turn_tp[0]);
    //g_icecfg.turn_tp[0].server = pj_str("123.59.204.198");
    g_icecfg.turn_tp[0].server = pj_str("127.0.0.1");
    g_icecfg.turn_tp[0].port = 3478;
    g_icecfg.turn_tp[0].af = pj_AF_INET();
    g_icecfg.turn_tp[0].conn_type = PJ_TURN_TP_UDP;
    /* end ice config start */

    pjmedia_ice_cb cb;
    // 注释有点非常误导或者本来就是错的
    // on_ice_complete2 说：如果这个两个回调都有，只有这个才回调
    // 但是实际上是两个都会回调，只有在特定条件下(可能是ice协商成公)才只回调2函数
    cb.on_ice_complete = on_ice_complete;
    cb.on_ice_complete2 = on_ice_complete2;

    pjmedia_transport *transport = NULL;
    status = pjmedia_ice_create3(med_endpt, "icetest", 1, &g_icecfg,
        &cb, //const pjmedia_ice_cb *cb
        0,
        med_endpt,
        &transport);
    printf("after create3------------------\n");

    
    do{
        pj_thread_sleep(1000);
    }while(iceState == 0);
    printf("after sleep------------------\n");
    
    
    pjmedia_transport_info tpinfo;
    pjmedia_transport_info_init(&tpinfo);
    pjmedia_transport_get_info(transport, &tpinfo);
    pjmedia_transport_info_get_spc_info(&tpinfo, PJMEDIA_TRANSPORT_TYPE_ICE);
    
    printf("after get info------------------\n");
    

    if(gRole == OFFER){
        pjmedia_sdp_session *sdp;
        createOffer(med_endpt, transport, &sdp, &tpinfo);
        setLocalDescription(sdp);
        gOpenFile = ANSWERSDP;
        sdp_from_file(&sdp);
        setRemoteDescription(sdp);
    }
    
    if(gRole == ANSWER){
        gOpenFile = OFFERSDP;
        pjmedia_sdp_session *rSdp, *lSdp;
        sdp_from_file(&rSdp);
        setRemoteDescription(rSdp);
        createAnswer(med_endpt, transport, &lSdp, rSdp, &tpinfo);
        setLocalDescription(lSdp);
    }
    
    input_confirm("confirm to negotiation:");

    pj_pool_t * icenegpool = pj_pool_create(&cp.factory, "iceneg", 2048, 1024, NULL);
    status = pjmedia_transport_media_start(transport, icenegpool, localSdp, remoteSdp, 0);
    assert(status == PJ_SUCCESS);
    
#if 0
    do{
        pj_thread_sleep(1000);
    }while(iceState != 2);
    printf("after sleep2------------------\n");
#endif

    char packet[120];
    while(1){
        memset(packet, 0, sizeof(packet));
        memset(packet, 0x31, 12);
        printf("input:");
        scanf("%s", packet+12);
        if(packet[12] == 'q'){
            break;
        }
        pjmedia_transport_send_rtp(transport, packet, strlen(packet));
    }
   
    input_confirm("quit");
}
