#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "rtspType.h"
#include "utils.h"
#include "rtsp.h"
#include "net.h"


inline static void RtspIncreaseCseq(RtspSession *sess);
inline static void RtspIncreaseCseq(RtspSession *sess)
{
    sess->cseq++;
    return;
}

int32_t RtspResponseStatus(char *response, char **error)
{
    int32_t size = 256, err_size = 0x00;
    int32_t offset = sizeof(RTSP_RESPONSE) - 1;
    char buf[8], *sep = NULL, *eol = NULL;
    *error = NULL;

    if (strncmp((const char*)response, (const char*)RTSP_RESPONSE, offset) != 0) {
        *error = calloc(1, size);
        snprintf((char *)*error, size, "Invalid RTSP response format");
        return -1;
    }

    sep = strchr((const char *)response+offset, ' ');
    if (!sep) {
        *error = calloc(1, size);
        snprintf((char *)*error, size, "Invalid RTSP response format");
        return -1;
    }

    memset(buf, '\0', sizeof(buf));
    strncpy((char *)buf, (const char *)(response+offset), sep-response-offset);

    eol = strchr(response, '\r');
    err_size = (eol - response) - offset - 1 - strlen(buf);
    *error = calloc(1, err_size + 1);
    if (NULL == *error){
        fprintf(stderr, "%s: Error calloc\n", __func__);
        return -1;
    }
    strncpy(*error, response + offset + 1 + strlen(buf), err_size);

    return atoi(buf);
}


int32_t RtspOptionsMsg(RtspSession *sess)
{
    int32_t num;
    int32_t ret = True;
    int32_t status;
    int32_t size = 4096;
    char *err = NULL;
    char buf[size];
    int32_t sock = sess->sockfd;

#ifdef RTSP_DEBUG
    printf("OPTIONS: command\n");
#endif

    memset(buf, '\0', sizeof(buf));
    num = snprintf(buf, size, CMD_OPTIONS, sess->ip, sess->port, sess->cseq);
    if (num < 0x00){
        fprintf(stderr, "%s : snprintf error!\n", __func__);
        return False;
    }
    num = RtspTcpSendMsg(sock, buf, (uint32_t)num);
    if (num < 0){
        fprintf(stderr, "%s : Send Error\n", __func__);
        return False;
    }
#ifdef RTSP_DEBUG
    printf("OPTIONS: request sent\n");
#endif

    memset(buf, '\0', sizeof(buf));
    num = RtspTcpRcvMsg(sock, buf, size-1);
    if (num <= 0) {
        printf("Error: Server did not respond properly, closing...");
        return False;
    }

    status = RtspResponseStatus(buf, &err);
    if (status == 200){
        printf("OPTIONS: response status %i (%i bytes)\n", status, num);
    }
    else{
        printf("OPTIONS: response status %i: %s\n", status, err);
        ret = False;
    }
    if (NULL != err)
        free(err);
    RtspIncreaseCseq(sess);

    return ret;
}

static void GetSdpVideoAcontrol(char *buf, uint32_t size, char *url)
{
    char *ptr = (char *)memmem((const void*)buf, size,
            (const void*)SDP_M_VIDEO, strlen(SDP_M_VIDEO)-1);
    if (NULL == ptr){
        fprintf(stderr, "Error: m=video not found!\n");
        return;
    }

    ptr = (char *)memmem((const void*)ptr, size,
            (const void*)SDP_A_CONTROL, strlen(SDP_A_CONTROL)-1);
    if (NULL == ptr){
        fprintf(stderr, "Error: a=control not found!\n");
        return;
    }

    char *endptr = (char *)memmem((const void*)ptr, size,
            (const void*)"\r\n", strlen("\r\n")-1);
    if (NULL == endptr){
        fprintf(stderr, "Error: %s not found!\n", "\r\n");
        return;
    }
    ptr += strlen(SDP_A_CONTROL);
    if ('*' == *ptr){
        /* a=control:* */
        printf("a=control:*\n");
        return;
    }else if (0x00 == memcmp((const void*)ptr, \
                (const void*)PROTOCOL_PREFIX, \
                strlen(PROTOCOL_PREFIX)-1)){
            /* a=control:rtsp://ip:port/track1 */
            memcpy((void *)url, (const void*)(ptr), (endptr-ptr));
            url[endptr-ptr] = '\0';
    }else{
        /*a=control:track1*/
        int32_t len = strlen(url);
        url[len] = '/';
        len++;
        char *p = url+len;
        memcpy((void *)p, (const void*)ptr, (endptr-ptr));
        url[len+endptr-ptr] = '\0';
    }

    return;
}

static int32_t ParseSdpProto(char *buf, uint32_t size, RtspSession *sess)
{
    GetSdpVideoAcontrol(buf, size, sess->url);
#ifdef RTSP_DEBUG
    printf("video url: %s\n", sess->url);
#endif
    return True;
}

int32_t RtspDescribeMsg(RtspSession *sess)
{
    int32_t num;
    int32_t ret = True;
    int32_t status;
    int32_t size = 4096;
    char *err;
    char buf[size];
    int32_t sock = sess->sockfd;

#ifdef RTSP_DEBUG
    printf("DESCRIBE: command\n");
#endif

    memset(buf, '\0', sizeof(buf));
    num = snprintf(buf, size, CMD_DESCRIBE, sess->url, sess->cseq);
    if (num < 0x00){
        fprintf(stderr, "%s : snprintf error!\n", __func__);
        return False;
    }

    num = RtspTcpSendMsg(sock, buf, (uint32_t)num);
    if (num < 0){
        fprintf(stderr, "%s : Send Error\n", __func__);
        return False;
    }

#ifdef RTSP_DEBUG
    printf("DESCRIBE: request sent\n");
#endif

    memset(buf, '\0', sizeof(buf));
    num = RtspTcpRcvMsg(sock, buf, size-1);
    if (num <= 0) {
        printf("Error: Server did not respond properly, closing...");
        return False;
    }


    status = RtspResponseStatus(buf, &err);
    if (status == 200) {
        printf("DESCRIBE: response status %i (%i bytes)\n", status, num);
    }
    else {
        printf("DESCRIBE: response status %i: %s\n", status, err);
        ret = False;
    }
    if (NULL != err)
        free(err);

    ParseSdpProto(buf, num, sess);
    RtspIncreaseCseq(sess);

    return ret;
}

static int32_t ParseTransport(char *buf, uint32_t size, RtspSession *sess)
{
    char *p = strstr(buf, "Transport: ");
    if (!p) {
        printf("SETUP: Error, Transport header not found\n");
        return False;
    }

    char *sep = strchr(p, ';');
    if (NULL == sep)
        return False;
    return True;
}

static int32_t ParseSessionID(char *buf, uint32_t size, RtspSession *sess)
{
    /* Session ID */
    char *p = strstr(buf, SETUP_SESSION);
    if (!p) {
        printf("SETUP: Session header not found\n");
        return False;
    }
    char *sep = strchr((const char *)p, ';');
    if (NULL == sep){
        sep = strchr((const char *)p, '\r');
    }
    memset(sess->sessid, '\0', sizeof(sess->sessid));
    strncpy(sess->sessid, p+sizeof(SETUP_SESSION)-1, sep-p-sizeof(SETUP_SESSION)+1);
#ifndef RTSP_DEBUG
    printf("sessid : %s\n", sess->sessid);
#endif
    return True;
}

int32_t RtspSetupMsg(RtspSession *sess)
{
    int32_t num, ret = True, status;
    int32_t size = 4096;
    char *err;
    char buf[size];
    int32_t sock = sess->sockfd;

#ifndef RTSP_DEBUG
    printf("SETUP: command\n");
#endif

    memset(buf, '\0', sizeof(buf));
    num = snprintf(buf, size, CMD_SETUP, sess->url, sess->cseq);
    if (num < 0x00){
        fprintf(stderr, "%s : snprintf error!\n", __func__);
        return False;
    }
    num = RtspTcpSendMsg(sock, buf, (uint32_t)num);
    if (num < 0){
        fprintf(stderr, "%s : Send Error\n", __func__);
        return False;
    }

#ifdef RTSP_DEBUG
    printf("SETUP: request sent\n");
#endif
    memset(buf, '\0', sizeof(buf));
    num = RtspTcpRcvMsg(sock, buf, size-1);
    if (num <= 0) {
        fprintf(stderr, "Error: Server did not respond properly, closing...");
        return False;
    }

    status = RtspResponseStatus(buf, &err);
    if (status == 200) {
        printf("SETUP: response status %i (%i bytes)\n", status, num);
        /*ParseTransport(buf, num, sess);*/
        ParseSessionID(buf, num, sess);
    }
    else {
        printf("SETUP: response status %i: %s\n", status, err);
        return False;
    }

    /* Fill session data */
    sess->packetization = 1; /* FIXME: project specific value */
    RtspIncreaseCseq(sess);
    return ret;
}

int32_t RtspPlayMsg(RtspSession *sess)
{
    int32_t num, ret = True, status, size=4096;
    char  *err, buf[size];
    int32_t sock = sess->sockfd;

#ifdef RTSP_DEBUG
    printf("PLAY: command\n");
#endif
    memset(buf, '\0', sizeof(buf));
    num = snprintf(buf, size, CMD_PLAY, sess->url, sess->cseq, sess->sessid);
    if (num < 0x00){
        fprintf(stderr, "%s : snprintf error!\n", __func__);
        return False;
    }
    num = RtspTcpSendMsg(sock, buf, (uint32_t)num);
    if (num < 0){
        fprintf(stderr, "%s : Send Error\n", __func__);
        return False;
    }

#ifdef RTSP_DEBUG
    printf("PLAY: request sent\n");
#endif

    memset(buf, '\0', sizeof(buf));
    num = RtspTcpRcvMsg(sock, buf, size-1);
    if (num <= 0) {
        fprintf(stderr, "Error: Server did not respond properly, closing...");
        return False;
    }

    status = RtspResponseStatus(buf, &err);
    if (status == 200) {
        printf("PLAY: response status %i (%i bytes)\n", status, num);
    }
    else {
        fprintf(stderr, "PLAY: response status %i: %s\n", status, err);
        ret = False;
    }

    RtspIncreaseCseq(sess);
    return ret;
}


int32_t RtspTeardownMsg(RtspSession *sess)
{
    int32_t num, ret = True, status, size=4096;
    char  *err, buf[size];
    int32_t sock = sess->sockfd;

#ifdef RTSP_DEBUG
    printf("TEARDOWN: command\n");
#endif
    memset(buf, '\0', sizeof(buf));
    num = snprintf(buf, size, CMD_TEARDOWN, sess->url, sess->cseq, sess->sessid);
    if (num < 0x00){
        fprintf(stderr, "%s : snprintf error!\n", __func__);
        return False;
    }
    num = RtspTcpSendMsg(sock, buf, (uint32_t)num);
    if (num < 0){
        fprintf(stderr, "%s : Send Error\n", __func__);
        return False;
    }

#ifdef RTSP_DEBUG
    printf("TEARDOWN: request sent\n");
#endif

    memset(buf, '\0', sizeof(buf));
    num = RtspTcpRcvMsg(sock, buf, size-1);
    if (num <= 0) {
        fprintf(stderr, "Error: Server did not respond properly, closing...");
        return False;
    }

    status = RtspResponseStatus(buf, &err);
    if (status == 200) {
        printf("TEARDOWN: response status %i (%i bytes)\n", status, num);
    }
    else {
        fprintf(stderr, "TEARDOWN: response status %i: %s\n", status, err);
        ret = False;
    }

    RtspIncreaseCseq(sess);
    return ret;
}

int32_t RtspStatusMachine(RtspSession *sess)
{
    do{
        switch(sess->status){
            case RTSP_START:
                if (False == RtspOptionsMsg(sess)){
                    fprintf(stderr, "Error: RtspOptionsMsg.\n");
                    return False;
                }
                sess->status = RTSP_OPTIONS;
                break;
            case RTSP_OPTIONS:
                if (False == RtspDescribeMsg(sess)){
                    fprintf(stderr, "Error: RtspDescribeMsg.\n");
                    return False;
                }
                sess->status = RTSP_DESCRIBE;
                break;
            case RTSP_DESCRIBE:
                if (False == RtspSetupMsg(sess)){
                    fprintf(stderr, "Error: RtspSetupMsg.\n");
                    return False;
                }
                sess->status = RTSP_SETUP;
                break;
            case RTSP_SETUP:
                if (False == RtspPlayMsg(sess)){
                    fprintf(stderr, "Error: RtspPlayMsg.\n");
                    return False;
                }
                sess->status = RTSP_PLAY;
                break;
            case RTSP_TEARDOWN:
                if (False == RtspTeardownMsg(sess)){
                    fprintf(stderr, "Error: RtspTeardownMsg.\n");
                    return False;
                }
                sess->status = RTSP_QUIT;
                break;
            case RTSP_QUIT:
                fprintf(stderr, "rtsp status : RTSP_QUIT!\n");
                return True;
            default:
                break;
        }
        usleep(1000);
    }while(1);

    return True;
}
