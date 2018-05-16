#include "myrtp.h"

static void app_perror(const char *sender, const char *title,
                       pj_status_t status)
{
    char errmsg[PJ_ERR_MSG_SIZE];
    
    pj_strerror(status, errmsg, sizeof(errmsg));
    PJ_LOG(3,(sender, "%s: %s [status=%d]", title, errmsg, status));
}

pj_status_t create_sdp( pj_pool_t *pool,
                              pjmedia_sdp_session **p_sdp)
{
    pj_time_val tv;
    pjmedia_sdp_session *sdp;
    pjmedia_sdp_media *m;
    pjmedia_sdp_attr *attr;
    
    PJ_ASSERT_RETURN(pool && p_sdp, PJ_EINVAL);
    
    /* Create and initialize basic SDP session */
    sdp = pj_pool_zalloc (pool, sizeof(pjmedia_sdp_session));
    
    pj_gettimeofday(&tv);
    sdp->origin.user = pj_str("-");
    sdp->origin.version = sdp->origin.id = tv.sec + 2208988800UL;
    sdp->origin.net_type = pj_str("IN");
    sdp->origin.addr_type = pj_str("IP4");
    sdp->origin.addr = *pj_gethostname();
    sdp->name = pj_str("pjsip");
    
    /* Since we only support one media stream at present, put the
     * SDP connection line in the session level.
     */
    sdp->conn = pj_pool_zalloc (pool, sizeof(pjmedia_sdp_conn));
    sdp->conn->net_type = pj_str("IN");
    sdp->conn->addr_type = pj_str("IP4");
    sdp->conn->addr = pj_str("127.0.0.1"); //TODO refect addr or relay addr
    
    
    /* SDP time and attributes. */
    sdp->time.start = sdp->time.stop = 0;
    sdp->attr_count = 0;
    
    /* Create media stream 0: */
    
    sdp->media_count = 1;
    m = pj_pool_zalloc (pool, sizeof(pjmedia_sdp_media));
    sdp->media[0] = m;
    
    /* Standard media info: */
    m->desc.media = pj_str("audio");
    m->desc.port = 5002;//TODO 这个port从ice协商来？
    m->desc.port_count = 1;
    m->desc.transport = pj_str("RTP/AVP");
    
    /* Add format and rtpmap for each codec. */
    m->desc.fmt_count = 1;
    m->attr_count = 0;
    
    {
        pjmedia_sdp_rtpmap rtpmap;
        char ptstr[10];
        
        sprintf(ptstr, "%d", 0); //TODO 0 is pcmu
        pj_strdup2(pool, &m->desc.fmt[0], ptstr);
        rtpmap.pt = m->desc.fmt[0];
        rtpmap.clock_rate = 8000; //TODO 8000
        rtpmap.enc_name = pj_str("PCMU");
        rtpmap.param.slen = 0;
        
        pjmedia_sdp_rtpmap_to_attr(pool, &rtpmap, &attr);
        m->attr[m->attr_count++] = attr;
    }
    
    /* Add sendrecv attribute. */
    attr = pj_pool_zalloc(pool, sizeof(pjmedia_sdp_attr));
    attr->name = pj_str("sendrecv");
    m->attr[m->attr_count++] = attr;
    
#if 1
    /*
     * Add support telephony event
     */
    m->desc.fmt[m->desc.fmt_count++] = pj_str("121");
    /* Add rtpmap. */
    attr = pj_pool_zalloc(pool, sizeof(pjmedia_sdp_attr));
    attr->name = pj_str("rtpmap");
    attr->value = pj_str("121 telephone-event/8000");
    m->attr[m->attr_count++] = attr;
    /* Add fmtp */
    attr = pj_pool_zalloc(pool, sizeof(pjmedia_sdp_attr));
    attr->name = pj_str("fmtp");
    attr->value = pj_str("121 0-15");
    m->attr[m->attr_count++] = attr;
#endif
    
    /* Done */
    *p_sdp = sdp;
    
    return PJ_SUCCESS;
}

int librtp_init(){
    pj_status_t status;
    
    status = pj_init();
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, status);
    
    status = pjlib_util_init();
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, status);
    
    return PJ_SUCCESS;
}
void librtp_uninit(){
    
}
/*
 * name: for log purpose
 */
int librtp_init_rtp(RtpMediaStream * s, char * name){
    memset(s, 0, sizeof(RtpMediaStream));
    s->audio.payload_type = -1;
    s->video.payload_type = -1;
    
    pj_caching_pool_init(&s->cp, &pj_pool_factory_default_policy, 0);
    s->pool = pj_pool_create(&s->cp.factory, name, 2000, 2000, NULL);
    
    pj_status_t status;
    //TODO ioqueue is NULL
    status = pjmedia_endpt_create(&s->cp.factory, NULL, 1, &s->med_endpt);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, status);
}

static int librtp_init_transport(RtpMediaStream * s){
    pj_status_t status;
    if(s->use_ice){
        status = 1;
        //transport_media_start 传入sdp协商
        // ice sdp和rtp sdp区别？
    }else{
        status = pjmedia_transport_udp_create(s->med_endpt, NULL, 4000,
                                              0, &s->transport);
    }
    if (status != PJ_SUCCESS)
        return status;
    return 0;
}

static int librtp_init_ice(RtpMediaStream * s){
    //s->use_ice = 1;
    
    return 0;
}

int librtp_set_transport(RtpMediaStream * s, pjmedia_transport * t){
    s->transport = t;
    return 0;
}

int librtp_set_audio(RtpMediaStream * s, MediaInfo * m){
    s->audio = *m;
    return 0;
}

int librtp_set_video(RtpMediaStream * s, MediaInfo * m){
    s->video = *m;
    return 0;
}

void librtp_release_sender(RtpMediaStream * s){
    if (s && s->pool) {
        pj_pool_release(s->pool);
        s->pool = NULL;
        pj_caching_pool_destroy(&s->cp);
    }
}

int librtp_put_video(RtpMediaStream * s, char * data, int dataSize){
    return 0;
}

int librtp_put_audio(RtpMediaStream * strm, char * data, int dataSize){
    enum { RTCP_INTERVAL = 5000, RTCP_RAND = 2000 };
    char packet[1500];
    unsigned msec_interval = 20;
    
    if(strm->time_inited == 0){
        strm->time_inited = 1;
        pj_get_timestamp_freq(&strm->freq);
        
        pj_get_timestamp(&strm->next_rtp);
        strm->next_rtp.u64 += (strm->freq.u64 * msec_interval / 1000);
        
        strm->next_rtcp = strm->next_rtp;
        strm->next_rtcp.u64 += (strm->freq.u64 * (RTCP_INTERVAL+(pj_rand()%RTCP_RAND)) / 1000);
    }
    
    pj_timestamp now;
    if (strm->next_rtp.u64 >= strm->next_rtcp.u64) {
        pj_get_timestamp(&now);
        if (strm->next_rtcp.u64 <= now.u64) {
            void *rtcp_pkt;
            int rtcp_len;
            pj_ssize_t size;
            pj_status_t status;
            
            /* Build RTCP packet */
            pjmedia_rtcp_build_rtcp(&strm->rtp_pair.rtcp_session, &rtcp_pkt, &rtcp_len);
            
            /* Send packet */
            size = rtcp_len;
            status = pjmedia_transport_send_rtcp(strm->transport,
                                                 rtcp_pkt, size);
            if (status != PJ_SUCCESS) {
                app_perror("myrtp.c", "Error sending RTCP packet", status);
            }
            
            /* Schedule next send */
            strm->next_rtcp.u64 += (strm->freq.u64 * (RTCP_INTERVAL+(pj_rand()%RTCP_RAND)) / 1000);
        }
    }
    
    pj_timestamp lesser;
    pj_time_val timeout;

    lesser = strm->next_rtp;
    pj_get_timestamp(&now);
    
    /* Determine how long to sleep */
    if (lesser.u64 <= now.u64) {
        timeout.sec = timeout.msec = 0;
        //printf("immediate "); fflush(stdout);
    } else {
        pj_uint64_t tick_delay;
        tick_delay = lesser.u64 - now.u64;
        timeout.sec = 0;
        timeout.msec = (pj_uint32_t)(tick_delay * 1000 / strm->freq.u64);
        pj_time_val_normalize(&timeout);
        
        //printf("%d:%03d ", timeout.sec, timeout.msec); fflush(stdout);
    }
    printf("timeout:%d %d\n", timeout.sec, timeout.msec);
    pj_thread_sleep(PJ_TIME_VAL_MSEC(timeout)); //TODO deal sleep
    
    //start to send rtp
    pj_status_t status;
    const void *p_hdr;
    const pjmedia_rtp_hdr *hdr;
    pj_ssize_t size;
    int hdrlen;
    
    /* Format RTP header */
    status = pjmedia_rtp_encode_rtp( &strm->rtp_pair.rtp_session, 0, //pt is 0 for pcmu
                                    0, /* marker bit */
                                    160,
                                    160,
                                    &p_hdr, &hdrlen);
    if (status == PJ_SUCCESS) {
        
        //PJ_LOG(4,(THIS_FILE, "\t\tTx seq=%d", pj_ntohs(hdr->seq)));
        
        hdr = (const pjmedia_rtp_hdr*) p_hdr;
        
        /* Copy RTP header to packet */
        pj_memcpy(packet, hdr, hdrlen);
        
        /* Zero the payload */
        pj_memcpy(packet+hdrlen, data, 160);
        
        /* Send RTP packet */
        size = hdrlen + 160;
        status = pjmedia_transport_send_rtp(strm->transport,
                                            packet, size);
        if (status != PJ_SUCCESS)
            app_perror("myrtp.c", "Error sending RTP packet", status);
        
    } else {
        pj_assert(!"RTP encode() error");
    }
    
    /* Update RTCP SR */
    pjmedia_rtcp_tx_rtp( &strm->rtp_pair.rtcp_session, 160);
    
    /* Schedule next send */
    strm->next_rtp.u64 += (msec_interval * strm->freq.u64 / 1000);
    
    
    return 0;
}

int librtp_init_p2p_transport_when180(RtpMediaStream * s){
    pjmedia_rtp_session_init(&s->rtp_pair.rtp_session, s->audio.payload_type,
                             pj_rand());
    
    pjmedia_rtcp_init(&s->rtp_pair.rtcp_session, "rtcp", s->audio.clock_rate,
                      160, 0); //TODO 160 instead by cacl
    
    //TODO init ice transport
    librtp_init_ice(s);
    
    pj_status_t status;
    pj_str_t ipstr = pj_str("127.0.0.1");
    status = pjmedia_transport_udp_create2(s->med_endpt,
                                           "siprtp",
                                           &ipstr,
                                           4000, 0, //TODO fixed 4000 now
                                           &s->transport);
    
    
    pj_sockaddr_in rtp_addr, rtcp_addr;
    pj_sockaddr_in_init(&rtp_addr, &ipstr, 5002);
    pj_sockaddr_in_init(&rtcp_addr, &ipstr, 5003);
    /* Attach media to transport */
    status = pjmedia_transport_attach(s->transport, s,
                                      &rtp_addr,
                                      &rtcp_addr,
                                      sizeof(pj_sockaddr_in),
                                      NULL,
                                      NULL);
    if (status != PJ_SUCCESS) {
        return status;
    }
    return 0;
}
