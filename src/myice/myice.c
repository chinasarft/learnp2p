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

static int app_perror(const char *sender, const char *title,
    pj_status_t status)
{
    char errmsg[PJ_ERR_MSG_SIZE];

    pj_strerror(status, errmsg, sizeof(errmsg));

    PJ_LOG(3, (sender, "%s: %s [code=%d]", title, errmsg, status));
    return 1;
}

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

    status = pjmedia_transport_encode_sdp(transport, sdppool, sdp, NULL, 0);
    assert(status == PJ_SUCCESS);

    memset(sdpStr, 0, 2048);
    pjmedia_sdp_print(sdp, sdpStr, 2048);
    printf("%s\n", sdpStr);
    //pjmedia_sdp_attr *candAttr;
    //pjmedia_sdp_attr_create(sdppool, "candidate", NULL);

}

void setLocalDescription(pjmedia_sdp_session *sdp) {
    localSdp = sdp;
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

    int sleepTime = 1000;
    if(argc == 2)
        sleepTime = atoi(argv[1]);
    do{
    pj_thread_sleep(sleepTime);
    }while(gatheringOk == 0);
    printf("after sleep------------------\n");
    pjmedia_transport_info tpinfo;
    pjmedia_transport_info_init(&tpinfo);
    pjmedia_transport_get_info(transport, &tpinfo);
    pjmedia_transport_info_get_spc_info(&tpinfo, PJMEDIA_TRANSPORT_TYPE_ICE);
    printf("after get info------------------\n");
    
    //这个调用一定要在on_ice_complete回调之后
    pj_pool_t * sdppool = pj_pool_create(&cp.factory, "sdp", 2048, 1024, NULL);
    status = pjmedia_transport_media_create(transport, sdppool, 0, NULL, 0);
    if (status != PJ_SUCCESS) {
        app_perror(THIS_FILE, "pjmedia_transport_media_create", status);
        return status;
    }
    printf("after media create------------------\n");

    pjmedia_sdp_session *sdp;
    createOffer(med_endpt, transport, &sdp, &tpinfo);
    setLocalDescription(sdp);

    //pjmedia_sdp_parse(pj_pool_t *pool, char *buf, pj_size_t len, pjmedia_sdp_session **p_sdp);
    //setRemoteDescription();

   
}
