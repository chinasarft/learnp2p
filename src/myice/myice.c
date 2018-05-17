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
int gatheringOk = 0;

#define OFFER 1
#define ANSWER 2
int gRole = OFFER;


void on_ice_complete2(pjmedia_transport *tp,
    pj_ice_strans_op op,
    pj_status_t status,
    void *user_data) {

    printf("--------->on_ice_complete2\n");

}

void on_ice_complete(pjmedia_transport *tp,
    pj_ice_strans_op op,
    pj_status_t status) {

    switch (op) {
        /** Initialization (candidate gathering) */
    case PJ_ICE_STRANS_OP_INIT:
        gatheringOk = 1;
        printf("--->gathering candidates finish\n");
        break;

        /** Negotiation */
    case PJ_ICE_STRANS_OP_NEGOTIATION:
        printf("--->PJ_ICE_STRANS_OP_NEGOTIATION\n");
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

void sdp_from_file(pjmedia_sdp_session ** sdp){
    
    char sdpfilepath[256] = {0};
    printf("input peer sdp file.(read from file):");
    scanf("%s\n", sdpfilepath);
    char remoteSdpStr[2048]={0};
    
    FILE * f = fopen(sdpfilepath, "rb");
    assert(f != NULL);
    
    fseek(f,0, SEEK_END);
    int flen = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    int rlen = fread(remoteSdpStr, 1, flen, f);
    assert(rlen == flen);
    
    pj_pool_t * sdpRemotepool = pj_pool_create(&cp.factory, "sdpremote", 2048, 1024, NULL);
    
    pj_status_t status;
    status = pjmedia_sdp_parse(sdpRemotepool, remoteSdpStr, strlen(remoteSdpStr), sdp);
    assert(status == PJ_SUCCESS);
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

    char * sdpStr = (char *)malloc(2048);
    memset(sdpStr, 0, 2048);
    pjmedia_sdp_print(sdp, sdpStr, 2048);
    printf("%s\n", sdpStr);

    status = pjmedia_transport_media_create(transport, sdppool, 0, NULL, 0);
    assert(status == PJ_SUCCESS);

    status = pjmedia_transport_encode_sdp(transport, sdppool, sdp, NULL, 0);
    assert(status == PJ_SUCCESS);

    memset(sdpStr, 0, 2048);
    pjmedia_sdp_print(sdp, sdpStr, 2048);
    printf("%s\n", sdpStr);
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
    
    char * sdpStr = (char *)malloc(2048);
    memset(sdpStr, 0, 2048);
    pjmedia_sdp_print(sdp, sdpStr, 2048);
    printf("%s\n", sdpStr);
    
    status = pjmedia_transport_media_create(transport, sdppool, 0, offer, 0);
    assert(status == PJ_SUCCESS);
    
    status = pjmedia_transport_encode_sdp(transport, sdppool, sdp, offer, 0);
    assert(status == PJ_SUCCESS);
    
    memset(sdpStr, 0, 2048);
    pjmedia_sdp_print(sdp, sdpStr, 2048);
    printf("%s\n", sdpStr);
}

void setLocalDescription(pjmedia_sdp_session *sdp) {
    localSdp = sdp;
}

void setRemoteDescription(pjmedia_sdp_session *sdp) {
    remoteSdp = sdp;
}

int main(int argc, char **argv) {
    pj_status_t status;
    status = pj_init();
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);
    status = pjlib_util_init();
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);

    pj_caching_pool_init(&cp, &pj_pool_factory_default_policy, 0);

    
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
    }while(roleOk != 0);

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

    g_icecfg.stun_tp_cnt = 0;
    //pj_ice_strans_stun_cfg_default(&g_icecfg.stun_tp[0]);

    pj_stun_config_init(&g_icecfg.stun_cfg, &cp.factory, 0,
        pjmedia_endpt_get_ioqueue(med_endpt), ht);

    g_icecfg.turn_tp_cnt = 1;
    g_icecfg.turn_tp[0].server = pj_str("123.59.204.198");
    g_icecfg.turn_tp[0].port = 3478;
    g_icecfg.turn_tp[0].af = pj_AF_INET();
    g_icecfg.turn_tp[0].conn_type = PJ_TURN_TP_UDP;
    pj_turn_sock_cfg_default(&g_icecfg.turn_tp[0].cfg);
    /* end ice config start */

    pjmedia_ice_cb cb;
    // 注释有点非常误导或者本来就是错的
    // on_ice_complete2 说：如果这个两个回调都有，只有这个才回调
    // 但是实际上是两个都会回调，只有在特定条件下(可能是ice协商成公)才只回调2函数
    cb.on_ice_complete = on_ice_complete;
    cb.on_ice_complete2 = on_ice_complete2;

    pjmedia_transport *transport = NULL;
    status = pjmedia_ice_create3(med_endpt, "icetest", 2, &g_icecfg,
        &cb, //const pjmedia_ice_cb *cb
        0,
        med_endpt,
        &transport);
    printf("after create3------------------\n");

    
    do{
        pj_thread_sleep(1000);
    }while(gatheringOk == 0);
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
        sdp_from_file(&sdp);
        setRemoteDescription(sdp);
    }
    
    if(gRole == ANSWER){
        pjmedia_sdp_session *rSdp, *lSdp;
        sdp_from_file(&rSdp);
        createAnswer(med_endpt, transport, &lSdp, rSdp, &tpinfo);
        setLocalDescription(lSdp);
    }

    pj_pool_t * icenegpool = pj_pool_create(&cp.factory, "iceneg", 2048, 1024, NULL);
    pjmedia_transport_media_start(transport, icenegpool, localSdp, remoteSdp, 0);
   
    printf("enter to quit");
    scanf("%s\n", (char *)&gRole);
}
