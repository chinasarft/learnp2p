#include <pjlib.h>
#include <pjlib-util.h>
#include <pjmedia.h>
#include <pjmedia-codec.h>
#include <pjmedia/transport_srtp.h>

#include <stdlib.h>	/* atoi() */
#include <stdio.h>

#define THIS_FILE	"mystream.c"
pj_caching_pool cp;

static int app_perror(const char *sender, const char *title,
                      pj_status_t status)
{
    char errmsg[PJ_ERR_MSG_SIZE];
    
    pj_strerror(status, errmsg, sizeof(errmsg));
    
    PJ_LOG(3, (sender, "%s: %s [code=%d]", title, errmsg, status));
    return 1;
}


/*
* Create stream based on the codec, dir, remote address, etc.
*/
static pj_status_t create_stream(pj_pool_t *pool,
                                 pjmedia_endpt *med_endpt,
                                 const pjmedia_codec_info *codec_info,
                                 pjmedia_dir dir,
                                 pj_uint16_t local_port,
                                 const pj_sockaddr_in *rem_addr,
                                 pjmedia_stream **p_stream)
{
    pjmedia_stream_info info;
    pjmedia_transport *transport = NULL;
    pj_status_t status;


    /* Reset stream info. */
    pj_bzero(&info, sizeof(info));

    /* Initialize stream info formats */
    info.type = PJMEDIA_TYPE_AUDIO;
    info.dir = dir;
    pj_memcpy(&info.fmt, codec_info, sizeof(pjmedia_codec_info));
    info.tx_pt = codec_info->pt;
    info.rx_pt = codec_info->pt;
    info.ssrc = pj_rand();

    /* Copy remote address */
    pj_memcpy(&info.rem_addr, rem_addr, sizeof(pj_sockaddr_in));

    /* If remote address is not set, set to an arbitrary address
    * (otherwise stream will assert).
    */
    if (info.rem_addr.addr.sa_family == 0) {
        const pj_str_t addr = pj_str("127.0.0.1");
        pj_sockaddr_in_init(&info.rem_addr.ipv4, &addr, 0);
    }

    pj_sockaddr_cp(&info.rem_rtcp, &info.rem_addr);
    pj_sockaddr_set_port(&info.rem_rtcp,
        pj_sockaddr_get_port(&info.rem_rtcp) + 1);


    status = pjmedia_transport_udp_create(med_endpt, NULL, local_port,
        0, &transport);
    if (status != PJ_SUCCESS)
        return status;

    /* Now that the stream info is initialized, we can create the
    * stream.
    */
    status = pjmedia_stream_create(med_endpt, pool, &info,
        transport,
        NULL, p_stream);

    if (status != PJ_SUCCESS) {
        app_perror(THIS_FILE, "Error creating stream", status);
        pjmedia_transport_close(transport);
        return status;
    }


    return PJ_SUCCESS;
}


/*
* main()
*/
int main(int argc, char *argv[])
{
    pj_status_t status;

    pjmedia_dir dir = PJMEDIA_DIR_ENCODING;
    pj_sockaddr_in remote_addr;
    pj_uint16_t local_port = 4000;

    /* init PJLIB : */
    status = pj_init();
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);
    /* Must create a pool factory before we can allocate any memory. */
    pj_caching_pool_init(&cp, &pj_pool_factory_default_policy, 0);

    pj_bzero(&remote_addr, sizeof(remote_addr));
    {
        char addrStr[] = "127.0.0.1:5002";
        pj_str_t ip = pj_str(strtok(addrStr, ":"));
        pj_uint16_t port = (pj_uint16_t)atoi(strtok(NULL, ":"));
        
        status = pj_sockaddr_in_init(&remote_addr, &ip, port);
        if (status != PJ_SUCCESS) {
            app_perror(THIS_FILE, "Invalid remote address", status);
            return 1;
        }
    }

    /*
    * Initialize media endpoint.
    * This will implicitly initialize PJMEDIA too.
    */
    pjmedia_endpt *med_endpt;
    status = pjmedia_endpt_create(&cp.factory, NULL, 1, &med_endpt);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);
    
    // 注册所有的音频，实际调用函数audio_codecs.c:pjmedia_codec_register_audio_codes
    status = pjmedia_codec_register_audio_codecs(med_endpt, NULL);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);
    
    
    // pjmedia_codec_register_audio_codecs从这个函数里面查找
    const pjmedia_codec_info *codec_info;
    pjmedia_codec_mgr_get_codec_info(pjmedia_endpt_get_codec_mgr(med_endpt),
                                     0, &codec_info);
    
    /* Create memory pool for application purpose */
    pj_pool_t *pool;
    pool = pj_pool_create(&cp.factory, "app", 4000, 4000, NULL);
    /* Create stream based on program arguments */
    pjmedia_stream *stream = NULL;
    status = create_stream(pool, med_endpt, codec_info, dir, local_port,
        &remote_addr,
        &stream);
    if (status != PJ_SUCCESS)
        goto on_exit;
    
    /* Get codec default param for info */
    pjmedia_codec_param codec_param;
    status = pjmedia_codec_mgr_get_default_param(
                                                 pjmedia_endpt_get_codec_mgr(med_endpt),
                                                 codec_info,
                                                 &codec_param);
    /* Should be ok, as create_stream() above succeeded */
    pj_assert(status == PJ_SUCCESS);

    /* Get the port interface of the stream */
    pjmedia_port *stream_port;
    status = pjmedia_stream_get_port(stream, &stream_port);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);


    /* Start streaming */
    status = pjmedia_stream_start(stream);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);


    /* Done */
    char addr[PJ_INET_ADDRSTRLEN] = {0};
    if (dir == PJMEDIA_DIR_ENCODING)
        printf("Stream is active, dir is send-only, sending to %s:%d\n",
            pj_inet_ntop2(pj_AF_INET(), &remote_addr.sin_addr, addr,
                sizeof(remote_addr)),
            pj_ntohs(remote_addr.sin_port));
    

    pj_oshandle_t audioFd;
    pj_pool_t * apool = pj_pool_create(&cp.factory, "afiletest", 2000, 2000, NULL);
    status = pj_file_open(apool, "/Users/liuye/Documents/p2p/build/src/mysiprtp/Debug/8000_1.mulaw", PJ_O_RDONLY, &audioFd);
    if(status != PJ_SUCCESS){
        printf("pj_file_open fail:%d\n", status);
        return status;
    }
    char abuf[1500] = {0};
    int cnt = 0;
    while(1){
        pj_ssize_t readLen = 160;
        status = pj_file_read(audioFd, abuf, &readLen);
        if(status != PJ_SUCCESS){
            printf("pj_file_read fail:%d\n", status);
            break;;
        }
        if(readLen == 160){
            cnt++;
            
            //TODO
            pjmedia_frame frame;
            pj_bzero(&frame, sizeof(frame));
            frame.type = PJMEDIA_FRAME_TYPE_AUDIO;
            frame.buf = abuf;
            frame.size = readLen;
            //frame.timestamp.u64 = cnt * 20 * 1000000;
            pj_get_timestamp(&frame.timestamp);
            printf("%08d:send %ld to rtp, ts:%lld\n",cnt, readLen, frame.timestamp.u64);
            pjmedia_port_put_frame(stream_port, &frame);
            pj_thread_sleep(20);
        }else{
            printf("pj_file_read less than one frame length:\n");
        }
    }


    /* Start deinitialization: */
on_exit:

    /* Destroy stream */
    if (stream) {
        pjmedia_transport *tp;

        tp = pjmedia_stream_get_transport(stream);
        pjmedia_stream_destroy(stream);

        pjmedia_transport_close(tp);
    }

 

    /* Release application pool */
    pj_pool_release(pool);

    /* Destroy media endpoint. */
    pjmedia_endpt_destroy(med_endpt);

    /* Destroy pool factory */
    pj_caching_pool_destroy(&cp);

    /* Shutdown PJLIB */
    pj_shutdown();


    return (status == PJ_SUCCESS) ? 0 : 1;
}
