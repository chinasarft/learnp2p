#include <pjsip.h>
#include <pjmedia.h>
#include <pjmedia-codec.h>
#include <pjsip_ua.h>
#include <pjsip_simple.h>
#include <pjlib-util.h>
#include <pjlib.h>
#define THIS_FILE  "simpleua.c"
//#include "util.h"
static int app_perror(const char *sender, const char *title,
    pj_status_t status)
{
    char errmsg[PJ_ERR_MSG_SIZE];

    pj_strerror(status, errmsg, sizeof(errmsg));

    PJ_LOG(3, (sender, "%s: %s [code=%d]", title, errmsg, status));
    return 1;
}

/* Dump memory pool usage. */
void dump_pool_usage(const char *app_name, pj_caching_pool *cp)
{
#if !defined(PJ_HAS_POOL_ALT_API) || PJ_HAS_POOL_ALT_API==0
    pj_pool_t   *p;
    pj_size_t    total_alloc = 0;
    pj_size_t    total_used = 0;

    /* Accumulate memory usage in active list. */
    p = (pj_pool_t*)cp->used_list.next;
    while (p != (pj_pool_t*)&cp->used_list) {
        total_alloc += pj_pool_get_capacity(p);
        total_used += pj_pool_get_used_size(p);
        p = p->next;
    }

    PJ_LOG(3, (app_name, "Total pool memory allocated=%d KB, used=%d KB",
        total_alloc / 1000,
        total_used / 1000));
#endif
}


//设置
#define AF      pj_AF_INET()// 如果使用IPV6，将此行改为pj_AF_INET6()
//必须设置PJ_HAS_IPV6
//并且操作系统也需要支持 IPv6.  */
#if 0
#define SIP_PORT    5080             //侦听的SIP端口
#define RTP_PORT    5000             // RTP端口
#else
#define SIP_PORT    5060            //侦听的SIP端口
#define RTP_PORT    4000            // RTP端口
#endif
#define MAX_MEDIA_CNT   2       //媒体数量，设置为1将支持音频，
//设置为2，即支持音频，又支持视频
//全局静态变量
static pj_bool_t        g_complete;    //退出状态
static pjsip_endpoint      *g_endpt;        // SIP终端
static pj_caching_pool       g_cp;          //全局pool factory
static pjmedia_endpt       *g_med_endpt;   // 媒体终端
static pjmedia_transport_info g_med_tpinfo[MAX_MEDIA_CNT];
//媒体传输用套接字信息
static pjmedia_transport    *g_med_transport[MAX_MEDIA_CNT];
//媒体流传输端口
static pjmedia_sock_info     g_sock_info[MAX_MEDIA_CNT];
//套接字信息组
//呼叫部分变量
static pjsip_inv_session    *g_inv;      //当前invite传话
static pjmedia_stream       *g_med_stream;  //语音流
static pjmedia_snd_port     *g_snd_port;   //声卡设备
#if defined(PJMEDIA_HAS_VIDEO)&& (PJMEDIA_HAS_VIDEO != 0)
staticpjmedia_vid_stream   *g_med_vstream; //视频流
static pjmedia_vid_port    *g_vid_capturer;//视频捕获设备
static pjmedia_vid_port    *g_vid_renderer;//视频播放设备
#endif  //PJMEDIA_HAS_VIDEO
                                           //函数声明
                                           //呼叫过程上，当SDP协商完成后，回调此函数
static void  call_on_media_update(pjsip_inv_session *inv,
    pj_status_t status);
//当invite会话状态变化后，回调此函数
static void  call_on_state_changed(pjsip_inv_session *inv,
    pjsip_event *e);
//当新对话建立后，回调此函数
static void  call_on_forked(pjsip_inv_session *inv, pjsip_event *e);
//对话之外，当收到请求时，回调此函数
static pj_bool_t  on_rx_request(pjsip_rx_data *rdata);
static pj_bool_t  on_rx_response(pjsip_rx_data *rdata);
static pj_bool_t  on_tx_request(pjsip_tx_data *rdata);
static pj_bool_t  on_tx_response(pjsip_tx_data *rdata);
//此PJSIP模块注册到应用用于处理对话或事务之外的请求，主要目的是处理INVITE请求消息，在那里将为它创建新的对话和INVITE会话
static pjsip_module  mod_simpleua =
{
    NULL, NULL,             /*prev, next.       */
    { "mod-simpleua", 12 },      /* Name.         */
    -1,                  /* Id            */
    PJSIP_MOD_PRIORITY_APPLICATION, /* Priority          */
    NULL,                /* load()            */
    NULL,                /* start()           */
    NULL,                /* stop()            */
    NULL,                /* unload()          */
    &on_rx_request,          /* on_rx_request()       */
    &on_rx_response,          /* on_rx_response()      */
    &on_tx_request,                /* on_tx_request.        */
    &on_tx_response,                /* on_tx_response()      */
    NULL,                /* on_tsx_state()        */
};
//呼入消息通知
static pj_bool_t  logging_on_rx_msg(pjsip_rx_data *rdata)
{
    PJ_LOG(4, (THIS_FILE, "logm:RX %d bytes%s from %s %s:%d:\n"
        "%.*s\n"
        "--end msg--",
        rdata->msg_info.len,
        pjsip_rx_data_get_info(rdata),
        rdata->tp_info.transport->type_name,
        rdata->pkt_info.src_name,
        rdata->pkt_info.src_port,
        (int)rdata->msg_info.len,
        rdata->msg_info.msg_buf));
    //此处必须返回false,否则其它消息将不被处理
    return PJ_FALSE;
}
//呼出消息通知
static pj_status_t  logging_on_tx_msg(pjsip_tx_data *tdata)
{
    /* Important note:
    *   tp_infofield is only valid after outgoing messages has passed
    *   transportlayer. So don't try to access tp_info when the module
    *   haslower priority than transport layer.
    */
    PJ_LOG(4, (THIS_FILE, "logm:TX %d bytes%s to %s %s:%d:\n"
        "%.*s\n"
        "--end msg--",
        (tdata->buf.cur - tdata->buf.start),
        pjsip_tx_data_get_info(tdata),
        tdata->tp_info.transport->type_name,
        tdata->tp_info.dst_name,
        tdata->tp_info.dst_port,
        (int)(tdata->buf.cur - tdata->buf.start),
        tdata->buf.start));
    /* Always return success, otherwise message will notget sent! */
    return PJ_SUCCESS;
}
//模块实例
static pjsip_module  msg_logger =
{
    NULL, NULL,              /* prev, next.      */
    { "mod-msg-log", 13 },       /* Name.        */
    -1,                  /* Id           */
    PJSIP_MOD_PRIORITY_TRANSPORT_LAYER - 1,/* Priority        */
    NULL,                /* load()       */
    NULL,                /* start()      */
    NULL,                /* stop()       */
    NULL,                /* unload()     */
    &logging_on_rx_msg,          /* on_rx_request()  */
    &logging_on_rx_msg,          /* on_rx_response() */
    &logging_on_tx_msg,          /* on_tx_request.   */
    &logging_on_tx_msg,          /* on_tx_response() */
    NULL,                /* on_tsx_state()   */
};
/*
* main()
*
* If called with argument, treat argument asSIP URL to be called.
* Otherwise wait for incoming calls.
*/
int main(int argc, char *argv[])
{
    if (argc != 1 && argc != 4) {
        printf("usage as:%s [sipport] [rtpport] [sip:ip:port]\n", argv[0]);
        return -1;
    }
    pj_pool_t  *pool = NULL;
    pj_status_t  status;
    unsigned i;
    /* 必须先初始化PJLIB*/
    status = pj_init();
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);
    pj_log_set_level(5);
    /* 再初始化PJLIB-UTIL*/
    status = pjlib_util_init();
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);
    /* 在分配内存之前必须创建pool factory*/
    pj_caching_pool_init(&g_cp, &pj_pool_factory_default_policy, 0);

    //创建全局终端
    {
        const  pj_str_t  *hostname;
        const  char  *endpt_name;
        //终端必须分配全局唯一名称，此处简单地使用主机名实现
        hostname = pj_gethostname();
        endpt_name = hostname->ptr;
        //创建终端
        status = pjsip_endpt_create(&g_cp.factory, endpt_name,
            &g_endpt);
        PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);
    }

    //添加UDP传输端口
    //如果已经存在了UDP套接字时，应用可使用pjsip_udp_transport_attach()函数启动UDP传输端口
    {
        pj_sockaddr  addr;
        int port = SIP_PORT;
        if (argc > 3)
            port = atoi(argv[1]);
        pj_sockaddr_init(AF, &addr, NULL, (pj_uint16_t)port);
        if (AF == pj_AF_INET()) {
            status = pjsip_udp_transport_start(g_endpt, &addr.ipv4, NULL,
                1, NULL);
        }
        else if (AF == pj_AF_INET6()) {
            status = pjsip_udp_transport_start6(g_endpt, &addr.ipv6, NULL,
                1, NULL);
        }
        else {
            status = PJ_EAFNOTSUP;
        }
        if (status != PJ_SUCCESS) {
            app_perror(THIS_FILE, "Unableto start UDP transport", status);
            return 1;
        }
    }

    //初始化事务层，将创建/初始化传输hash表
    status = pjsip_tsx_layer_init_module(g_endpt);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);
    //初始化UA层模块，将创建/初始化对话hash表
    status = pjsip_ua_init_module(g_endpt, NULL);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);

    //初始化invite会话模块，将初始化会话附加参数，例如与事件相关的回调函数
    //on_state_changed与on_new_session是应用必须支持的回调函数
    //我们可以让应用程序在on_media_update()回调函数中，启动媒体传输
    {
        pjsip_inv_callback  inv_cb;
        // 初始化INVITE会话回调
        pj_bzero(&inv_cb, sizeof(inv_cb));
        inv_cb.on_state_changed = &call_on_state_changed;
        inv_cb.on_new_session = &call_on_forked;
        inv_cb.on_media_update = &call_on_media_update;
        //初始化INVITE会话模块
        status = pjsip_inv_usage_init(g_endpt, &inv_cb);
        PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);
    }

    //初始化100rel支持
    status = pjsip_100rel_init_module(g_endpt);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, status);

    //注册用于接收呼入请求的自己模块
    status = pjsip_endpt_register_module(g_endpt, &mod_simpleua);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);

    //注册消息日志模块
    status = pjsip_endpt_register_module(g_endpt, &msg_logger);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);

    //初始化媒体终端，它将隐性地启动PJMEDIA的相关功能
#if PJ_HAS_THREADS
    status = pjmedia_endpt_create(&g_cp.factory, NULL, 1, &g_med_endpt);
#else
    status = pjmedia_endpt_create(&cp.factory,
        pjsip_endpt_get_ioqueue(g_endpt),
        0, &g_med_endpt);
#endif
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);

    //给媒体终端增加PCMA/PCMU编解码器
#if defined(PJMEDIA_HAS_G711_CODEC)&& PJMEDIA_HAS_G711_CODEC!=0
    status = pjmedia_codec_g711_init(g_med_endpt);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);
#endif

#if defined(PJMEDIA_HAS_VIDEO)&& (PJMEDIA_HAS_VIDEO != 0)
    //初始化视频子系统
    pool = pjmedia_endpt_create_pool(g_med_endpt, "Video subsystem", 512, 512);
    status = pjmedia_video_format_mgr_create(pool, 64, 0, NULL);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);
    status = pjmedia_converter_mgr_create(pool, NULL);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);
    status = pjmedia_vid_codec_mgr_create(pool, NULL);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);
    status = pjmedia_vid_dev_subsys_init(&cp.factory);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);
#   if defined(PJMEDIA_HAS_OPENH264_CODEC) &&PJMEDIA_HAS_OPENH264_CODEC != 0
    status = pjmedia_codec_openh264_vid_init(NULL, &cp.factory);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);
#   endif
#  if defined(PJMEDIA_HAS_FFMPEG_VID_CODEC) &&PJMEDIA_HAS_FFMPEG_VID_CODEC!=0
    //初始化ffmpeg视频编解码器
    status = pjmedia_codec_ffmpeg_vid_init(NULL, &cp.factory);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);
#  endif  /* PJMEDIA_HAS_FFMPEG_VID_CODEC */
#endif  /* PJMEDIA_HAS_VIDEO */

    //创建用于RTP/RTCP套接字发送/接收的媒体传输端口
    //每个呼叫均需要一个媒体传输端口，应用程序可以选择地复用相同的媒体传输端口用于后续呼叫
    int rtpport = RTP_PORT;
    if (argc > 3)
        rtpport = atoi(argv[2]);
    for (i = 0; i < PJ_ARRAY_SIZE(g_med_transport); ++i) {
        status = pjmedia_transport_udp_create3(g_med_endpt, AF, NULL, NULL,
            rtpport + i * 2, 0,
            &g_med_transport[i]);
        if (status != PJ_SUCCESS) {
            app_perror(THIS_FILE, "Unableto create media transport", status);
            return 1;
        }
        //取媒体传输端口的套接字信息(地址，端口)，我们需要利用它们创建SDP
        pjmedia_transport_info_init(&g_med_tpinfo[i]);
        pjmedia_transport_get_info(g_med_transport[i], &g_med_tpinfo[i]);
        pj_memcpy(&g_sock_info[i], &g_med_tpinfo[i].sock_info,
            sizeof(pjmedia_sock_info));
    }
    //如果提供呼叫的URL，则立即创建呼叫
    if (argc >= 3) {
        pj_sockaddr hostaddr;
        char hostip[PJ_INET6_ADDRSTRLEN + 2];
        char temp[80];
        pj_str_t dst_uri = pj_str(argv[3]);
        pj_str_t local_uri;
        pjsip_dialog *dlg;
        pjmedia_sdp_session *local_sdp;
        pjsip_tx_data *tdata;
        if (pj_gethostip(AF, &hostaddr) != PJ_SUCCESS) {
            app_perror(THIS_FILE, "Unableto retrieve local host IP", status);
            return 1;
        }
        pj_sockaddr_print(&hostaddr, hostip, sizeof(hostip), 2);
        pj_ansi_sprintf(temp, "<sip:simpleuac@%s:%d>",
            hostip, SIP_PORT);
        local_uri = pj_str(temp);
        //创建UAC对话
        status = pjsip_dlg_create_uac(pjsip_ua_instance(),
            &local_uri,  /* local URI */
            &local_uri,  /* local Contact */
            &dst_uri,    /* remote URI */
            &dst_uri,    /* remote target */
            &dlg);        /* dialog */
        if (status != PJ_SUCCESS) {
            app_perror(THIS_FILE, "Unableto create UAC dialog", status);
            return 1;
        }
        //如果怕呼出的INVITE被对方怀疑，我们可以在对话中加入凭证，如下例：
        /*
        {
        pjsip_cred_info cred[1];
        cred[0].realm     = pj_str("sip.server.realm");
        cred[0].scheme    = pj_str("digest");
        cred[0].username  = pj_str("theuser");
        cred[0].data_type =PJSIP_CRED_DATA_PLAIN_PASSWD;
        cred[0].data      = pj_str("thepassWord");
        pjsip_auth_clt_set_credentials(&dlg->auth_sess, 1, cred);
        }
        */
        /* Get the SDP body to be put in the outgoingINVITE, by asking
        *media endpoint to create one for us.
        */
        status = pjmedia_endpt_create_sdp(g_med_endpt,     /* the media endpt   */
            dlg->pool,       /* pool.     */
            MAX_MEDIA_CNT,  /* # of streams  */
            g_sock_info,     /* RTP sock info */
            &local_sdp);     /* the SDP result    */
        PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);
        //创建INVITE传话，方便地将SDP作为初始参数传递给会话
        status = pjsip_inv_create_uac(dlg, local_sdp, 0, &g_inv);
        PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);
        /* If we want the initial INVITE to travel tospecific SIP proxies,
        *then we should put the initial dialog's route set here. The final
        *route set will be updated once a dialog has been established.
        * Toset the dialog's initial route set, we do it with something
        *like this:
        *
        {
        pjsip_route_hdr route_set;
        pjsip_route_hdr *route;
        const pj_str_t hname = {"Route", 5 };
        char *uri ="sip:proxy.server;lr";
        pj_list_init(&route_set);
        route = pjsip_parse_hdr( dlg->pool,&hname,
        uri, strlen(uri),
        NULL);
        PJ_ASSERT_RETURN(route != NULL, 1);
        pj_list_push_back(&route_set,route);
        pjsip_dlg_set_route_set(dlg,&route_set);
        }
        *
        *Note that Route URI SHOULD have an ";lr" parameter!
        */
        //创建INVITE初始请求
        //此INVITE请求须包含完整的请求和SDP内容
        status = pjsip_inv_invite(g_inv, &tdata);
        PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);
        //发送INVITE请求
        //INVITE传话状态会通过回调传回
        status = pjsip_inv_send_msg(g_inv, tdata);
        PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);
    }
    else {
        //没有外呼的URL
        PJ_LOG(3, (THIS_FILE, "Ready toaccept incoming calls..."));
    }
    //循环，直到某个呼叫完成
    for (; !g_complete;) {
        pj_time_val timeout = { 0, 10 };
        pjsip_endpt_handle_events(g_endpt, &timeout);
    }
    //退出时，转储当前内存使用情况
    dump_pool_usage(THIS_FILE, &g_cp);
    //销毁音频端口
    //因为音频端口有线程存/取帧数据，故需要在流之前销毁音频端口
    if (g_snd_port)
        pjmedia_snd_port_destroy(g_snd_port);
#if defined(PJMEDIA_HAS_VIDEO)&& (PJMEDIA_HAS_VIDEO != 0)
    //销毁视频端口
    if (g_vid_capturer)
        pjmedia_vid_port_destroy(g_vid_capturer);
    if (g_vid_renderer)
        pjmedia_vid_port_destroy(g_vid_renderer);
#endif
    //销毁流
    if (g_med_stream)
        pjmedia_stream_destroy(g_med_stream);
#if defined(PJMEDIA_HAS_VIDEO)&& (PJMEDIA_HAS_VIDEO != 0)
    if (g_med_vstream)
        pjmedia_vid_stream_destroy(g_med_vstream);
    //ffmpeg
#   if defined(PJMEDIA_HAS_FFMPEG_VID_CODEC) &&PJMEDIA_HAS_FFMPEG_VID_CODEC!=0
    pjmedia_codec_ffmpeg_vid_deinit();
#   endif
#   if defined(PJMEDIA_HAS_OPENH264_CODEC) &&PJMEDIA_HAS_OPENH264_CODEC != 0
    pjmedia_codec_openh264_vid_deinit();
#   endif
#endif
    //销毁媒体传输端口
    for (i = 0; i < MAX_MEDIA_CNT; ++i) {
        if (g_med_transport[i])
            pjmedia_transport_close(g_med_transport[i]);
    }
    //完结媒体终端
    if (g_med_endpt)
        pjmedia_endpt_destroy(g_med_endpt);
    //完结sip终端
    if (g_endpt)
        pjsip_endpt_destroy(g_endpt);
    //释放pool
    if (pool)
        pj_pool_release(pool);
    return 0;
}
//当INVITE会话状态改变时的回调函数。
//此函数是在INVITE传话模块初始时注册，我们通常在此得到INVITE传话中断，并退出应用。
static void  call_on_state_changed(pjsip_inv_session *inv,
    pjsip_event *e)
{
    PJ_UNUSED_ARG(e);
    if (inv->state == PJSIP_INV_STATE_DISCONNECTED) {
        PJ_LOG(3, (THIS_FILE, "CallDISCONNECTED [reason=%d (%s)]",
            inv->cause,
            pjsip_get_status_text(inv->cause)->ptr));
        PJ_LOG(3, (THIS_FILE, "One callcompleted, application quitting..."));
        g_complete = 1;
    }
    else {
        PJ_LOG(3, (THIS_FILE, "Call statechanged to %s",
            pjsip_inv_state_name(inv->state)));
    }
}
//当会话复制完成后，此函数被回调
static void  call_on_forked(pjsip_inv_session *inv, pjsip_event *e)
{
    /* To be done... */
    PJ_UNUSED_ARG(inv);
    PJ_UNUSED_ARG(e);
}
static pj_bool_t  on_tx_request(pjsip_tx_data *rdata)
{
    PJ_LOG(4, (THIS_FILE, "suamod on_tx_request:"));
    return PJ_SUCCESS;
}
static pj_bool_t  on_tx_response(pjsip_tx_data *rdata)
{
    PJ_LOG(4, (THIS_FILE, "suamod on_tx_response:"));
    return PJ_SUCCESS;
}
static pj_bool_t  on_rx_response(pjsip_rx_data *rdata)
{
    PJ_LOG(4, (THIS_FILE, "suamod on_rx_response:"));
    return PJ_FALSE;
}
//当外部呼叫的任意对话或事务到达时，此函数被回调。我们可以控制想接的请求，其他的回应500拒绝
static pj_bool_t  on_rx_request(pjsip_rx_data *rdata)
{
    PJ_LOG(4, (THIS_FILE, "suamod on_rx_request:"));
    pj_sockaddr hostaddr;
    char temp[80], hostip[PJ_INET6_ADDRSTRLEN];
    pj_str_t local_uri;
    pjsip_dialog *dlg;
    pjmedia_sdp_session *local_sdp;
    pjsip_tx_data *tdata;
    unsigned options = 0;
    pj_status_t status;
    //以500回应（无状态）任何非INVITE请求
    if (rdata->msg_info.msg->line.req.method.id != PJSIP_INVITE_METHOD) {
        if (rdata->msg_info.msg->line.req.method.id != PJSIP_ACK_METHOD) {
            pj_str_t reason = pj_str("Simple UA unable to handle "
                "this request");
            pjsip_endpt_respond_stateless(g_endpt, rdata,
                500, &reason,
                NULL, NULL);
        }
        return PJ_TRUE;
    }
    //如果此INVITE会话已经处理，则拒绝
    if (g_inv) {
        pj_str_t reason = pj_str("Another callis in progress");
        pjsip_endpt_respond_stateless(g_endpt, rdata,
            500, &reason,
            NULL, NULL);
        return PJ_TRUE;
    }
    //效验下我们要控制的请求
    status = pjsip_inv_verify_request(rdata, &options, NULL, NULL,
        g_endpt, NULL);
    if (status != PJ_SUCCESS) {
        pj_str_t reason = pj_str("Sorry SimpleUA can not handle this INVITE");
        pjsip_endpt_respond_stateless(g_endpt, rdata,
            500, &reason,
            NULL, NULL);
        return PJ_TRUE;
    }
    //生成联系URI
    if (pj_gethostip(AF, &hostaddr) != PJ_SUCCESS) {
        app_perror(THIS_FILE, "Unable toretrieve local host IP", status);
        return PJ_TRUE;
    }
    pj_sockaddr_print(&hostaddr, hostip, sizeof(hostip), 2);
    pj_ansi_sprintf(temp, "<sip:simpleuas@%s:%d>",
        hostip, SIP_PORT);
    local_uri = pj_str(temp);
    //创建UAS对话
    status = pjsip_dlg_create_uas_and_inc_lock(pjsip_ua_instance(),
        rdata,
        &local_uri, /* contact */
        &dlg);
    if (status != PJ_SUCCESS) {
        pjsip_endpt_respond_stateless(g_endpt, rdata, 500, NULL,
            NULL, NULL);
        return PJ_TRUE;
    }
    //从媒体终端得到媒体能力集
    status = pjmedia_endpt_create_sdp(g_med_endpt, rdata->tp_info.pool,
        MAX_MEDIA_CNT, g_sock_info, &local_sdp);
    pj_assert(status == PJ_SUCCESS);
    if (status != PJ_SUCCESS) {
        pjsip_dlg_dec_lock(dlg);
        return PJ_TRUE;
    }
    //传递UAS会话和SDP能力集，创建INVITE会话
    status = pjsip_inv_create_uas(dlg, rdata, local_sdp, 0, &g_inv);
    pj_assert(status == PJ_SUCCESS);
    if (status != PJ_SUCCESS) {
        pjsip_dlg_dec_lock(dlg);
        return PJ_TRUE;
    }
    //INVITE传话已经创建，释放对话锁定
    pjsip_dlg_dec_lock(dlg);
    //首先发送180回应
    //最先发送的回应必须用pjsip_inv_initial_answer()创建，后面相同事务的回应必须用pjsip_inv_answer()
    status = pjsip_inv_initial_answer(g_inv, rdata,
        180,
        NULL, NULL, &tdata);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, PJ_TRUE);
    //发送180回应 
    status = pjsip_inv_send_msg(g_inv, tdata);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, PJ_TRUE);
    //现在创建200回应
    status = pjsip_inv_answer(g_inv,
        200, NULL,    /* st_code and st_text */
        NULL,     /* SDP already specified */
        &tdata);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, PJ_TRUE);
    //发送200回应
    status = pjsip_inv_send_msg(g_inv, tdata);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, PJ_TRUE);
    //完成。当呼叫中断时，将在回调中收到报告
    return PJ_TRUE;
}
//当SDP协商完成时，收到回调。我们要到此回调内快速启动媒体
static void  call_on_media_update(pjsip_inv_session *inv,
    pj_status_t status)
{
    pjmedia_stream_info stream_info;
    const pjmedia_sdp_session *local_sdp;
    const pjmedia_sdp_session *remote_sdp;
    pjmedia_port *media_port;
    if (status != PJ_SUCCESS) {
        app_perror(THIS_FILE, "SDPnegotiation has failed", status);
        /* Here we should disconnect call if we're not inthe middle
        * ofinitializing an UAS dialog and if this is not a re-INVITE.
        */
        return;
    }
    //取本地和远程SDP。我们需要这些SDP创建媒体会话
    status = pjmedia_sdp_neg_get_active_local(inv->neg, &local_sdp);
    status = pjmedia_sdp_neg_get_active_remote(inv->neg, &remote_sdp);
    //SDP中的音频媒体描述创建流信息
    status = pjmedia_stream_info_from_sdp(&stream_info, inv->dlg->pool,
        g_med_endpt,
        local_sdp, remote_sdp, 0);
    if (status != PJ_SUCCESS) {
        app_perror(THIS_FILE, "Unable tocreate audio stream info", status);
        return;
    }
    //如果需要，在创建流之前，我们可以更改流信息中的设置，如jitter设置、编解码器设置等等。
    //通过流信息和媒体套接字，轻松创建新的音频媒体流
    status = pjmedia_stream_create(g_med_endpt, inv->dlg->pool, &stream_info,
        g_med_transport[0], NULL, &g_med_stream);
    if (status != PJ_SUCCESS) {
        app_perror(THIS_FILE, "Unable tocreate audio stream", status);
        return;
    }
    //启动音频流
    status = pjmedia_stream_start(g_med_stream);
    if (status != PJ_SUCCESS) {
        app_perror(THIS_FILE, "Unable tostart audio stream", status);
        return;
    }
    //取音频流的媒体端口接口。媒体端口接口基本结构包括get_frame()和put_frame()函数，
    //我们可以将此媒体端口接口连接到会议桥，或直接到一个用于录音/放音的声卡设备
    pjmedia_stream_get_port(g_med_stream, &media_port);
    //创建声卡端口
    pjmedia_snd_port_create(inv->pool,
        PJMEDIA_AUD_DEFAULT_CAPTURE_DEV,
        PJMEDIA_AUD_DEFAULT_PLAYBACK_DEV,
        PJMEDIA_PIA_SRATE(&media_port->info),//时间速率
        PJMEDIA_PIA_CCNT(&media_port->info),//通话数
        PJMEDIA_PIA_SPF(&media_port->info), //每帧采样
        PJMEDIA_PIA_BITS(&media_port->info),//每次采样bit数
        0,
        &g_snd_port);
    if (status != PJ_SUCCESS) {
        app_perror(THIS_FILE, "Unable tocreate sound port", status);
        PJ_LOG(3, (THIS_FILE, "%d %d %d%d",
            PJMEDIA_PIA_SRATE(&media_port->info),/* clock rate       */
            PJMEDIA_PIA_CCNT(&media_port->info),/* channel count    */
            PJMEDIA_PIA_SPF(&media_port->info), /* samples per frame*/
            PJMEDIA_PIA_BITS(&media_port->info) /* bits per sample  */
            ));
        return;
    }
    status = pjmedia_snd_port_connect(g_snd_port, media_port);
    //取会话中第2个流的媒体端口接口，视频流的话，我们可以将此媒体端口接口直接连接到渲染/捕获视频设备
#if defined(PJMEDIA_HAS_VIDEO)&& (PJMEDIA_HAS_VIDEO != 0)
    if (local_sdp->media_count > 1) {
        pjmedia_vid_stream_info vstream_info;
        pjmedia_vid_port_param vport_param;
        pjmedia_vid_port_param_default(&vport_param);
        //通过SDP中的视频媒体描述合建视频信息
        status = pjmedia_vid_stream_info_from_sdp(&vstream_info,
            inv->dlg->pool, g_med_endpt,
            local_sdp, remote_sdp, 1);
        if (status != PJ_SUCCESS) {
            app_perror(THIS_FILE, "Unable to create video stream info", status);
            return;
        }
        //如果需要，在创建视频流之前，我们可以更改流信息中的设置，如jitter设置、编解码器设置等等。
        //通过流信息和媒体套接字参数，创建新的视频媒体流
        status = pjmedia_vid_stream_create(g_med_endpt, NULL, &vstream_info,
            g_med_transport[1], NULL,
            &g_med_vstream);
        if (status != PJ_SUCCESS) {
            app_perror(THIS_FILE, "Unable to create video stream", status);
            return;
        }
        //启动视频流
        status = pjmedia_vid_stream_start(g_med_vstream);
        if (status != PJ_SUCCESS) {
            app_perror(THIS_FILE, "Unable to start video stream", status);
            return;
        }
        if (vstream_info.dir & PJMEDIA_DIR_DECODING) {
            status = pjmedia_vid_dev_default_param(
                inv->pool, PJMEDIA_VID_DEFAULT_RENDER_DEV,
                &vport_param.vidparam);
            if (status != PJ_SUCCESS) {
                app_perror(THIS_FILE, "Unable to getdefault param of video "
                    "rendererdevice", status);
                return;
            }
            //取解码的视频流
            pjmedia_vid_stream_get_port(g_med_vstream, PJMEDIA_DIR_DECODING,
                &media_port);
            //设置格式
            pjmedia_format_copy(&vport_param.vidparam.fmt,
                &media_port->info.fmt);
            vport_param.vidparam.dir = PJMEDIA_DIR_RENDER;
            vport_param.active = PJ_TRUE;
            //创建渲染端口
            status = pjmedia_vid_port_create(inv->pool, &vport_param,
                &g_vid_renderer);
            if (status != PJ_SUCCESS) {
                app_perror(THIS_FILE, "Unable tocreate video renderer device",
                    status);
                return;
            }
            //连接渲染端口到媒体端口
            status = pjmedia_vid_port_connect(g_vid_renderer, media_port,
                PJ_FALSE);
            if (status != PJ_SUCCESS) {
                app_perror(THIS_FILE, "Unable toconnect renderer to stream",
                    status);
                return;
            }
        }
        //创建捕获设备
        if (vstream_info.dir & PJMEDIA_DIR_ENCODING) {
            status = pjmedia_vid_dev_default_param(
                inv->pool, PJMEDIA_VID_DEFAULT_CAPTURE_DEV,
                &vport_param.vidparam);
            if (status != PJ_SUCCESS) {
                app_perror(THIS_FILE, "Unable to getdefault param of video "
                    "capturedevice", status);
                return;
            }
            //取编码的视频流端口
            pjmedia_vid_stream_get_port(g_med_vstream, PJMEDIA_DIR_ENCODING,
                &media_port);
            //从流信息中取捕获格式
            pjmedia_format_copy(&vport_param.vidparam.fmt,
                &media_port->info.fmt);
            vport_param.vidparam.dir = PJMEDIA_DIR_CAPTURE;
            vport_param.active = PJ_TRUE;
            //创建录音端口
            status = pjmedia_vid_port_create(inv->pool, &vport_param,
                &g_vid_capturer);
            if (status != PJ_SUCCESS) {
                app_perror(THIS_FILE, "Unable tocreate video capture device",
                    status);
                return;
            }
            //连接录音媒体端口
            status = pjmedia_vid_port_connect(g_vid_capturer, media_port,
                PJ_FALSE);
            if (status != PJ_SUCCESS) {
                app_perror(THIS_FILE, "Unable toconnect capturer to stream",
                    status);
                return;
            }
        }
        //启动流
        if (g_vid_renderer) {
            status = pjmedia_vid_port_start(g_vid_renderer);
            if (status != PJ_SUCCESS) {
                app_perror(THIS_FILE, "Unable tostart video renderer",
                    status);
                return;
            }
        }
        if (g_vid_capturer) {
            status = pjmedia_vid_port_start(g_vid_capturer);
            if (status != PJ_SUCCESS) {
                app_perror(THIS_FILE, "Unable tostart video capturer",
                    status);
                return;
            }
        }
    }
#endif  /* PJMEDIA_HAS_VIDEO */
    //媒体部分完成
}