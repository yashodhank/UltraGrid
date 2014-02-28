/**
 * @file   video_rxtx.h
 * @author Martin Pulec     <pulec@cesnet.cz>
 */
/*
 * Copyright (c) 2013-2014 CESNET z.s.p.o.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, is permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of CESNET nor the names of its contributors may be
 *    used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESSED OR IMPLIED WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef VIDEO_RXTX_H_
#define VIDEO_RXTX_H_

#include "module.h"

struct display;
struct module;
struct video_compress;
struct video_export;
struct video_frame;

enum rxtx_protocol {
        ULTRAGRID_RTP,
        IHDTV,
        SAGE,
        H264_STD
};

struct rx_tx {
        enum rxtx_protocol protocol;
        const char *name;
        void (*send)(void *, struct video_frame *);
        void (*done)(void *);
        void *(*receiver_thread)(void *);
};

extern struct rx_tx ultragrid_rtp;
extern struct rx_tx sage_rxtx;
extern struct rx_tx h264_rtp;

struct sage_rxtx_state {
        struct video_desc saved_vid_desc;
        struct display *sage_tx_device;
        pthread_t thread_id;
};

struct h264_rtp_state {
        int connections_count;
        struct rtp **network_devices;
        struct tx *tx;
};

class video_rxtx {
public:
        video_rxtx(struct module *parent, struct video_export *video_exporter,
                        const char *requested_compression);
        virtual ~video_rxtx();
        void send(struct video_frame *);
        static const char *get_name(enum rxtx_protocol);
        static void *receiver_thread(void *arg) {
                video_rxtx *rxtx = static_cast<video_rxtx *>(arg);
                return rxtx->get_receiver_thread()(arg);
        }
        bool supports_receiving() {
                return get_receiver_thread() != NULL;
        }
protected:
        bool m_paused;
        struct module m_sender_mod;
        struct module m_receiver_mod;
private:
        virtual void send_frame(struct video_frame *) = 0;
        virtual void *(*get_receiver_thread())(void *arg) = 0;
        static void *sender_thread(void *args);
        void *sender_loop();
        virtual void process_message(struct msg_sender *) {
        }

        struct compress_state *m_compression;
        pthread_mutex_t m_lock;
        struct video_export *m_video_exporter;

        pthread_t m_thread_id;
};

#endif // VIDEO_RXTX_H_

