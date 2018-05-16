/* Include all headers. */
#include <pjsip.h>
#include <pjmedia.h>
#include <pjmedia-codec.h>
#include <pjsip_ua.h>
#include <pjsip_simple.h>
#include <pjlib-util.h>
#include <pjlib.h>

#include <stdlib.h>

typedef struct _MediaInfo{
    int payload_type;
    int clock_rate;
}MediaInfo;

typedef struct _RtpRtcpPair{
    pjmedia_rtp_session  rtp_session;
    pjmedia_rtcp_session rtcp_session;
}RtpRtcpPair;

typedef struct _RtpMediaStream{
    pjmedia_transport * transport;
    RtpRtcpPair rtp_pair;
    pj_caching_pool     cp;
    pj_pool_t        *pool;
    pjmedia_endpt    *med_endpt;
    int use_ice;
    MediaInfo audio;
    MediaInfo video;
    pj_timestamp freq, next_rtp, next_rtcp;
    int time_inited;
}RtpMediaStream;

typedef struct _SdpInfo{
    //TODO which args from invoker?
    int i;
}SdpInfo;


int create_sdp( pj_pool_t *pool, pjmedia_sdp_session **p_sdp);
int librtp_init();
int librtp_init_rtp(RtpMediaStream * s, char * name);

int librtp_set_audio(RtpMediaStream * s, MediaInfo * m);

int librtp_set_video(RtpMediaStream * s, MediaInfo * m);
int librtp_init_p2p_transport_when180(RtpMediaStream * s);
int librtp_put_audio(RtpMediaStream * s, char * data, int dataSize);
