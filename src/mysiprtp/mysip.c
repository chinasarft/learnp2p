#include "myrtp.h"

/* Application's global variables */
static struct app
{
    unsigned         max_calls;
    unsigned         call_gap;
    unsigned         uac_calls;
    unsigned         duration;
    int             sip_port;
    int             rtp_start_port;
    pj_str_t         local_addr;
    pj_str_t         local_uri;
    pj_str_t         local_contact;
    pj_str_t         sip_server;
    
    int             app_log_level;
    int             log_level;
    char        *log_filename;
    char        *report_filename;
    
    pj_str_t         uri_to_call;
    
    pj_caching_pool     cp;
    pj_pool_t        *pool;
    
    pjsip_endpoint    *sip_endpt;
    pj_bool_t         thread_quit;
    pj_thread_t        *sip_thread[1];
    
    RtpMediaStream media_stream;
} app;

/* Init application options */
static pj_status_t init_options(int argc, char *argv[])
{
    static char ip_addr[PJ_INET_ADDRSTRLEN];
    static char local_uri[64];
    
    enum { OPT_START,
        OPT_APP_LOG_LEVEL, OPT_LOG_FILE,
        OPT_A_PT, OPT_A_NAME, OPT_A_CLOCK, OPT_A_BITRATE, OPT_A_PTIME,
        OPT_REPORT_FILE };
    
    struct pj_getopt_option long_options[] = {
        { "server-ip",        1, 0, 's' },
        { "gap",            1, 0, 'g' },
        { "call-report",    0, 0, 'R' },
        { "duration",        1, 0, 'd' },
        { "local-port",        1, 0, 'p' },
        { "rtp-port",        1, 0, 'r' },
        { "ip-addr",        1, 0, 'i' },
        { "log-level",        1, 0, 'l' },
        { "app-log-level",  1, 0, OPT_APP_LOG_LEVEL },
        { NULL, 0, 0, 0 },
    };
    int c;
    int option_index;
    
    /* Get local IP address for the default IP address */
    {
        const pj_str_t *hostname;
        pj_sockaddr_in tmp_addr;
        
        hostname = pj_gethostname();
        pj_sockaddr_in_init(&tmp_addr, hostname, 0);
        pj_inet_ntop(pj_AF_INET(), &tmp_addr.sin_addr, ip_addr,
                     sizeof(ip_addr));
    }
    
    /* Init defaults */
    app.max_calls = 1;
    app.sip_port = 5060;
    app.rtp_start_port = 4000;
    app.local_addr = pj_str(ip_addr);
    app.log_level = 5;
    app.app_log_level = 3;
    app.log_filename = NULL;
    
    /* Parse options */
    pj_optind = 0;
    while((c=pj_getopt_long(argc,argv, "c:d:p:r:i:l:g:qR",
                            long_options, &option_index))!=-1)
    {
        switch (c) {
            case 's':
                app.sip_server = pj_str(pj_optarg);
                break;
            case 'g':
                app.call_gap = atoi(pj_optarg);
                break;
            case 'd':
                app.duration = atoi(pj_optarg);
                break;
                
            case 'p':
                app.sip_port = atoi(pj_optarg);
                break;
            case 'r':
                app.rtp_start_port = atoi(pj_optarg);
                break;
            case 'i':
                app.local_addr = pj_str(pj_optarg);
                break;
                
            case 'l':
                app.log_level = atoi(pj_optarg);
                break;
            case OPT_APP_LOG_LEVEL:
                app.app_log_level = atoi(pj_optarg);
                break;
            case OPT_LOG_FILE:
                app.log_filename = pj_optarg;
                break;
                
            default:
                //puts(USAGE);
                return 1;
        }
    }
    
    /* Check if URL is specified */
    if (pj_optind < argc)
        app.uri_to_call = pj_str(argv[pj_optind]);
    
    /* Build local URI and contact */
    pj_ansi_sprintf( local_uri, "sip:%s:%d", app.local_addr.ptr, app.sip_port);
    app.local_uri = pj_str(local_uri);
    app.local_contact = app.local_uri;
    
    
    return PJ_SUCCESS;
}

int main(int argc, char **argv){
    librtp_init();
    librtp_init_rtp(&app.media_stream, "rtptest");
    pjmedia_sdp_session *sdp = NULL;
    create_sdp(app.media_stream.pool,  &sdp);
    
    char str[4096]={0};
    pjmedia_sdp_print(sdp, str, sizeof(str));
    printf("%s\n", str);
    
    MediaInfo m;
    m.payload_type = 0;
    m.clock_rate = 8000;
    librtp_set_audio(&app.media_stream, &m);
    
    librtp_init_p2p_transport_when180(&app.media_stream);
    
    pj_status_t status;
    pj_oshandle_t fd;
    pj_pool_t * pool = pj_pool_create(&app.media_stream.cp.factory, "filetest", 2000, 2000, NULL);
    status = pj_file_open(pool, "/Users/liuye/Documents/p2p/build/src/mysiprtp/Debug/8000_1.mulaw", PJ_O_RDONLY, &fd);
    if(status != PJ_SUCCESS){
        printf("pj_file_open fail:%d\n", status);
        return status;
    }
    char * buf = pj_pool_alloc(pool, 1500);
    int cnt = 0;
    while(status == PJ_SUCCESS)
    {
        pj_ssize_t readLen = 160;
        status = pj_file_read(fd, buf, &readLen);
        if(status != PJ_SUCCESS){
            printf("pj_file_read fail:%d\n", status);
            continue;
        }
        if(readLen != 160){
            printf("pj_file_read less than one frame length:\n");
            status = 1;
            continue;
        }
        printf("%08d, send %d to rtp\n", cnt, readLen);
        librtp_put_audio(&app.media_stream, buf, (int)readLen);
        cnt++;
    }
    return 0;
}
