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


//����
#define AF      pj_AF_INET()// ���ʹ��IPV6�������и�Ϊpj_AF_INET6()
//��������PJ_HAS_IPV6
//���Ҳ���ϵͳҲ��Ҫ֧�� IPv6.  */
#if 0
#define SIP_PORT    5080             //������SIP�˿�
#define RTP_PORT    5000             // RTP�˿�
#else
#define SIP_PORT    5060            //������SIP�˿�
#define RTP_PORT    4000            // RTP�˿�
#endif
#define MAX_MEDIA_CNT   2       //ý������������Ϊ1��֧����Ƶ��
//����Ϊ2����֧����Ƶ����֧����Ƶ
//ȫ�־�̬����
static pj_bool_t        g_complete;    //�˳�״̬
static pjsip_endpoint      *g_endpt;        // SIP�ն�
static pj_caching_pool       g_cp;          //ȫ��pool factory
static pjmedia_endpt       *g_med_endpt;   // ý���ն�
static pjmedia_transport_info g_med_tpinfo[MAX_MEDIA_CNT];
//ý�崫�����׽�����Ϣ
static pjmedia_transport    *g_med_transport[MAX_MEDIA_CNT];
//ý��������˿�
static pjmedia_sock_info     g_sock_info[MAX_MEDIA_CNT];
//�׽�����Ϣ��
//���в��ֱ���
static pjsip_inv_session    *g_inv;      //��ǰinvite����
static pjmedia_stream       *g_med_stream;  //������
static pjmedia_snd_port     *g_snd_port;   //�����豸
#if defined(PJMEDIA_HAS_VIDEO)&& (PJMEDIA_HAS_VIDEO != 0)
staticpjmedia_vid_stream   *g_med_vstream; //��Ƶ��
static pjmedia_vid_port    *g_vid_capturer;//��Ƶ�����豸
static pjmedia_vid_port    *g_vid_renderer;//��Ƶ�����豸
#endif  //PJMEDIA_HAS_VIDEO
                                           //��������
                                           //���й����ϣ���SDPЭ����ɺ󣬻ص��˺���
static void  call_on_media_update(pjsip_inv_session *inv,
    pj_status_t status);
//��invite�Ự״̬�仯�󣬻ص��˺���
static void  call_on_state_changed(pjsip_inv_session *inv,
    pjsip_event *e);
//���¶Ի������󣬻ص��˺���
static void  call_on_forked(pjsip_inv_session *inv, pjsip_event *e);
//�Ի�֮�⣬���յ�����ʱ���ص��˺���
static pj_bool_t  on_rx_request(pjsip_rx_data *rdata);
static pj_bool_t  on_rx_response(pjsip_rx_data *rdata);
static pj_bool_t  on_tx_request(pjsip_tx_data *rdata);
static pj_bool_t  on_tx_response(pjsip_tx_data *rdata);
//��PJSIPģ��ע�ᵽӦ�����ڴ���Ի�������֮���������ҪĿ���Ǵ���INVITE������Ϣ�������ｫΪ�������µĶԻ���INVITE�Ự
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
//������Ϣ֪ͨ
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
    //�˴����뷵��false,����������Ϣ����������
    return PJ_FALSE;
}
//������Ϣ֪ͨ
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
//ģ��ʵ��
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
    /* �����ȳ�ʼ��PJLIB*/
    status = pj_init();
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);
    pj_log_set_level(5);
    /* �ٳ�ʼ��PJLIB-UTIL*/
    status = pjlib_util_init();
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);
    /* �ڷ����ڴ�֮ǰ���봴��pool factory*/
    pj_caching_pool_init(&g_cp, &pj_pool_factory_default_policy, 0);

    //����ȫ���ն�
    {
        const  pj_str_t  *hostname;
        const  char  *endpt_name;
        //�ն˱������ȫ��Ψһ���ƣ��˴��򵥵�ʹ��������ʵ��
        hostname = pj_gethostname();
        endpt_name = hostname->ptr;
        //�����ն�
        status = pjsip_endpt_create(&g_cp.factory, endpt_name,
            &g_endpt);
        PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);
    }

    //���UDP����˿�
    //����Ѿ�������UDP�׽���ʱ��Ӧ�ÿ�ʹ��pjsip_udp_transport_attach()��������UDP����˿�
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

    //��ʼ������㣬������/��ʼ������hash��
    status = pjsip_tsx_layer_init_module(g_endpt);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);
    //��ʼ��UA��ģ�飬������/��ʼ���Ի�hash��
    status = pjsip_ua_init_module(g_endpt, NULL);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);

    //��ʼ��invite�Ựģ�飬����ʼ���Ự���Ӳ������������¼���صĻص�����
    //on_state_changed��on_new_session��Ӧ�ñ���֧�ֵĻص�����
    //���ǿ�����Ӧ�ó�����on_media_update()�ص������У�����ý�崫��
    {
        pjsip_inv_callback  inv_cb;
        // ��ʼ��INVITE�Ự�ص�
        pj_bzero(&inv_cb, sizeof(inv_cb));
        inv_cb.on_state_changed = &call_on_state_changed;
        inv_cb.on_new_session = &call_on_forked;
        inv_cb.on_media_update = &call_on_media_update;
        //��ʼ��INVITE�Ựģ��
        status = pjsip_inv_usage_init(g_endpt, &inv_cb);
        PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);
    }

    //��ʼ��100rel֧��
    status = pjsip_100rel_init_module(g_endpt);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, status);

    //ע�����ڽ��պ���������Լ�ģ��
    status = pjsip_endpt_register_module(g_endpt, &mod_simpleua);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);

    //ע����Ϣ��־ģ��
    status = pjsip_endpt_register_module(g_endpt, &msg_logger);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);

    //��ʼ��ý���նˣ��������Ե�����PJMEDIA����ع���
#if PJ_HAS_THREADS
    status = pjmedia_endpt_create(&g_cp.factory, NULL, 1, &g_med_endpt);
#else
    status = pjmedia_endpt_create(&cp.factory,
        pjsip_endpt_get_ioqueue(g_endpt),
        0, &g_med_endpt);
#endif
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);

    //��ý���ն�����PCMA/PCMU�������
#if defined(PJMEDIA_HAS_G711_CODEC)&& PJMEDIA_HAS_G711_CODEC!=0
    status = pjmedia_codec_g711_init(g_med_endpt);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);
#endif

#if defined(PJMEDIA_HAS_VIDEO)&& (PJMEDIA_HAS_VIDEO != 0)
    //��ʼ����Ƶ��ϵͳ
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
    //��ʼ��ffmpeg��Ƶ�������
    status = pjmedia_codec_ffmpeg_vid_init(NULL, &cp.factory);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);
#  endif  /* PJMEDIA_HAS_FFMPEG_VID_CODEC */
#endif  /* PJMEDIA_HAS_VIDEO */

    //��������RTP/RTCP�׽��ַ���/���յ�ý�崫��˿�
    //ÿ�����о���Ҫһ��ý�崫��˿ڣ�Ӧ�ó������ѡ��ظ�����ͬ��ý�崫��˿����ں�������
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
        //ȡý�崫��˿ڵ��׽�����Ϣ(��ַ���˿�)��������Ҫ�������Ǵ���SDP
        pjmedia_transport_info_init(&g_med_tpinfo[i]);
        pjmedia_transport_get_info(g_med_transport[i], &g_med_tpinfo[i]);
        pj_memcpy(&g_sock_info[i], &g_med_tpinfo[i].sock_info,
            sizeof(pjmedia_sock_info));
    }
    //����ṩ���е�URL����������������
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
        //����UAC�Ի�
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
        //����º�����INVITE���Է����ɣ����ǿ����ڶԻ��м���ƾ֤����������
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
        //����INVITE����������ؽ�SDP��Ϊ��ʼ�������ݸ��Ự
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
        //����INVITE��ʼ����
        //��INVITE��������������������SDP����
        status = pjsip_inv_invite(g_inv, &tdata);
        PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);
        //����INVITE����
        //INVITE����״̬��ͨ���ص�����
        status = pjsip_inv_send_msg(g_inv, tdata);
        PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);
    }
    else {
        //û�������URL
        PJ_LOG(3, (THIS_FILE, "Ready toaccept incoming calls..."));
    }
    //ѭ����ֱ��ĳ���������
    for (; !g_complete;) {
        pj_time_val timeout = { 0, 10 };
        pjsip_endpt_handle_events(g_endpt, &timeout);
    }
    //�˳�ʱ��ת����ǰ�ڴ�ʹ�����
    dump_pool_usage(THIS_FILE, &g_cp);
    //������Ƶ�˿�
    //��Ϊ��Ƶ�˿����̴߳�/ȡ֡���ݣ�����Ҫ����֮ǰ������Ƶ�˿�
    if (g_snd_port)
        pjmedia_snd_port_destroy(g_snd_port);
#if defined(PJMEDIA_HAS_VIDEO)&& (PJMEDIA_HAS_VIDEO != 0)
    //������Ƶ�˿�
    if (g_vid_capturer)
        pjmedia_vid_port_destroy(g_vid_capturer);
    if (g_vid_renderer)
        pjmedia_vid_port_destroy(g_vid_renderer);
#endif
    //������
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
    //����ý�崫��˿�
    for (i = 0; i < MAX_MEDIA_CNT; ++i) {
        if (g_med_transport[i])
            pjmedia_transport_close(g_med_transport[i]);
    }
    //���ý���ն�
    if (g_med_endpt)
        pjmedia_endpt_destroy(g_med_endpt);
    //���sip�ն�
    if (g_endpt)
        pjsip_endpt_destroy(g_endpt);
    //�ͷ�pool
    if (pool)
        pj_pool_release(pool);
    return 0;
}
//��INVITE�Ự״̬�ı�ʱ�Ļص�������
//�˺�������INVITE����ģ���ʼʱע�ᣬ����ͨ���ڴ˵õ�INVITE�����жϣ����˳�Ӧ�á�
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
//���Ự������ɺ󣬴˺������ص�
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
//���ⲿ���е�����Ի������񵽴�ʱ���˺������ص������ǿ��Կ�����ӵ����������Ļ�Ӧ500�ܾ�
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
    //��500��Ӧ����״̬���κη�INVITE����
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
    //�����INVITE�Ự�Ѿ�������ܾ�
    if (g_inv) {
        pj_str_t reason = pj_str("Another callis in progress");
        pjsip_endpt_respond_stateless(g_endpt, rdata,
            500, &reason,
            NULL, NULL);
        return PJ_TRUE;
    }
    //Ч��������Ҫ���Ƶ�����
    status = pjsip_inv_verify_request(rdata, &options, NULL, NULL,
        g_endpt, NULL);
    if (status != PJ_SUCCESS) {
        pj_str_t reason = pj_str("Sorry SimpleUA can not handle this INVITE");
        pjsip_endpt_respond_stateless(g_endpt, rdata,
            500, &reason,
            NULL, NULL);
        return PJ_TRUE;
    }
    //������ϵURI
    if (pj_gethostip(AF, &hostaddr) != PJ_SUCCESS) {
        app_perror(THIS_FILE, "Unable toretrieve local host IP", status);
        return PJ_TRUE;
    }
    pj_sockaddr_print(&hostaddr, hostip, sizeof(hostip), 2);
    pj_ansi_sprintf(temp, "<sip:simpleuas@%s:%d>",
        hostip, SIP_PORT);
    local_uri = pj_str(temp);
    //����UAS�Ի�
    status = pjsip_dlg_create_uas_and_inc_lock(pjsip_ua_instance(),
        rdata,
        &local_uri, /* contact */
        &dlg);
    if (status != PJ_SUCCESS) {
        pjsip_endpt_respond_stateless(g_endpt, rdata, 500, NULL,
            NULL, NULL);
        return PJ_TRUE;
    }
    //��ý���ն˵õ�ý��������
    status = pjmedia_endpt_create_sdp(g_med_endpt, rdata->tp_info.pool,
        MAX_MEDIA_CNT, g_sock_info, &local_sdp);
    pj_assert(status == PJ_SUCCESS);
    if (status != PJ_SUCCESS) {
        pjsip_dlg_dec_lock(dlg);
        return PJ_TRUE;
    }
    //����UAS�Ự��SDP������������INVITE�Ự
    status = pjsip_inv_create_uas(dlg, rdata, local_sdp, 0, &g_inv);
    pj_assert(status == PJ_SUCCESS);
    if (status != PJ_SUCCESS) {
        pjsip_dlg_dec_lock(dlg);
        return PJ_TRUE;
    }
    //INVITE�����Ѿ��������ͷŶԻ�����
    pjsip_dlg_dec_lock(dlg);
    //���ȷ���180��Ӧ
    //���ȷ��͵Ļ�Ӧ������pjsip_inv_initial_answer()������������ͬ����Ļ�Ӧ������pjsip_inv_answer()
    status = pjsip_inv_initial_answer(g_inv, rdata,
        180,
        NULL, NULL, &tdata);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, PJ_TRUE);
    //����180��Ӧ 
    status = pjsip_inv_send_msg(g_inv, tdata);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, PJ_TRUE);
    //���ڴ���200��Ӧ
    status = pjsip_inv_answer(g_inv,
        200, NULL,    /* st_code and st_text */
        NULL,     /* SDP already specified */
        &tdata);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, PJ_TRUE);
    //����200��Ӧ
    status = pjsip_inv_send_msg(g_inv, tdata);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, PJ_TRUE);
    //��ɡ��������ж�ʱ�����ڻص����յ�����
    return PJ_TRUE;
}
//��SDPЭ�����ʱ���յ��ص�������Ҫ���˻ص��ڿ�������ý��
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
    //ȡ���غ�Զ��SDP��������Ҫ��ЩSDP����ý��Ự
    status = pjmedia_sdp_neg_get_active_local(inv->neg, &local_sdp);
    status = pjmedia_sdp_neg_get_active_remote(inv->neg, &remote_sdp);
    //SDP�е���Ƶý��������������Ϣ
    status = pjmedia_stream_info_from_sdp(&stream_info, inv->dlg->pool,
        g_med_endpt,
        local_sdp, remote_sdp, 0);
    if (status != PJ_SUCCESS) {
        app_perror(THIS_FILE, "Unable tocreate audio stream info", status);
        return;
    }
    //�����Ҫ���ڴ�����֮ǰ�����ǿ��Ը�������Ϣ�е����ã���jitter���á�����������õȵȡ�
    //ͨ������Ϣ��ý���׽��֣����ɴ����µ���Ƶý����
    status = pjmedia_stream_create(g_med_endpt, inv->dlg->pool, &stream_info,
        g_med_transport[0], NULL, &g_med_stream);
    if (status != PJ_SUCCESS) {
        app_perror(THIS_FILE, "Unable tocreate audio stream", status);
        return;
    }
    //������Ƶ��
    status = pjmedia_stream_start(g_med_stream);
    if (status != PJ_SUCCESS) {
        app_perror(THIS_FILE, "Unable tostart audio stream", status);
        return;
    }
    //ȡ��Ƶ����ý��˿ڽӿڡ�ý��˿ڽӿڻ����ṹ����get_frame()��put_frame()������
    //���ǿ��Խ���ý��˿ڽӿ����ӵ������ţ���ֱ�ӵ�һ������¼��/�����������豸
    pjmedia_stream_get_port(g_med_stream, &media_port);
    //���������˿�
    pjmedia_snd_port_create(inv->pool,
        PJMEDIA_AUD_DEFAULT_CAPTURE_DEV,
        PJMEDIA_AUD_DEFAULT_PLAYBACK_DEV,
        PJMEDIA_PIA_SRATE(&media_port->info),//ʱ������
        PJMEDIA_PIA_CCNT(&media_port->info),//ͨ����
        PJMEDIA_PIA_SPF(&media_port->info), //ÿ֡����
        PJMEDIA_PIA_BITS(&media_port->info),//ÿ�β���bit��
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
    //ȡ�Ự�е�2������ý��˿ڽӿڣ���Ƶ���Ļ������ǿ��Խ���ý��˿ڽӿ�ֱ�����ӵ���Ⱦ/������Ƶ�豸
#if defined(PJMEDIA_HAS_VIDEO)&& (PJMEDIA_HAS_VIDEO != 0)
    if (local_sdp->media_count > 1) {
        pjmedia_vid_stream_info vstream_info;
        pjmedia_vid_port_param vport_param;
        pjmedia_vid_port_param_default(&vport_param);
        //ͨ��SDP�е���Ƶý�������Ͻ���Ƶ��Ϣ
        status = pjmedia_vid_stream_info_from_sdp(&vstream_info,
            inv->dlg->pool, g_med_endpt,
            local_sdp, remote_sdp, 1);
        if (status != PJ_SUCCESS) {
            app_perror(THIS_FILE, "Unable to create video stream info", status);
            return;
        }
        //�����Ҫ���ڴ�����Ƶ��֮ǰ�����ǿ��Ը�������Ϣ�е����ã���jitter���á�����������õȵȡ�
        //ͨ������Ϣ��ý���׽��ֲ����������µ���Ƶý����
        status = pjmedia_vid_stream_create(g_med_endpt, NULL, &vstream_info,
            g_med_transport[1], NULL,
            &g_med_vstream);
        if (status != PJ_SUCCESS) {
            app_perror(THIS_FILE, "Unable to create video stream", status);
            return;
        }
        //������Ƶ��
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
            //ȡ�������Ƶ��
            pjmedia_vid_stream_get_port(g_med_vstream, PJMEDIA_DIR_DECODING,
                &media_port);
            //���ø�ʽ
            pjmedia_format_copy(&vport_param.vidparam.fmt,
                &media_port->info.fmt);
            vport_param.vidparam.dir = PJMEDIA_DIR_RENDER;
            vport_param.active = PJ_TRUE;
            //������Ⱦ�˿�
            status = pjmedia_vid_port_create(inv->pool, &vport_param,
                &g_vid_renderer);
            if (status != PJ_SUCCESS) {
                app_perror(THIS_FILE, "Unable tocreate video renderer device",
                    status);
                return;
            }
            //������Ⱦ�˿ڵ�ý��˿�
            status = pjmedia_vid_port_connect(g_vid_renderer, media_port,
                PJ_FALSE);
            if (status != PJ_SUCCESS) {
                app_perror(THIS_FILE, "Unable toconnect renderer to stream",
                    status);
                return;
            }
        }
        //���������豸
        if (vstream_info.dir & PJMEDIA_DIR_ENCODING) {
            status = pjmedia_vid_dev_default_param(
                inv->pool, PJMEDIA_VID_DEFAULT_CAPTURE_DEV,
                &vport_param.vidparam);
            if (status != PJ_SUCCESS) {
                app_perror(THIS_FILE, "Unable to getdefault param of video "
                    "capturedevice", status);
                return;
            }
            //ȡ�������Ƶ���˿�
            pjmedia_vid_stream_get_port(g_med_vstream, PJMEDIA_DIR_ENCODING,
                &media_port);
            //������Ϣ��ȡ�����ʽ
            pjmedia_format_copy(&vport_param.vidparam.fmt,
                &media_port->info.fmt);
            vport_param.vidparam.dir = PJMEDIA_DIR_CAPTURE;
            vport_param.active = PJ_TRUE;
            //����¼���˿�
            status = pjmedia_vid_port_create(inv->pool, &vport_param,
                &g_vid_capturer);
            if (status != PJ_SUCCESS) {
                app_perror(THIS_FILE, "Unable tocreate video capture device",
                    status);
                return;
            }
            //����¼��ý��˿�
            status = pjmedia_vid_port_connect(g_vid_capturer, media_port,
                PJ_FALSE);
            if (status != PJ_SUCCESS) {
                app_perror(THIS_FILE, "Unable toconnect capturer to stream",
                    status);
                return;
            }
        }
        //������
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
    //ý�岿�����
}