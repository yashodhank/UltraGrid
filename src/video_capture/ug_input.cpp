/**
 * @file   video_capture/ug_input.cpp
 * @author Martin Pulec     <pulec@cesnet.cz>
 */
/*
 * Copyright (c) 2014 CESNET, z. s. p. o.
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

#include "config.h"
#include "config_unix.h"
#include "config_win32.h"
#include "debug.h"
#include "host.h"
#include "hd-rum-translator/hd-rum-decompress.h" // frame_recv_delegate
#include "video.h"
#include "video_capture.h"
#include "video_capture/ug_input.h"
#include "video_display.h"
#include "video_rxtx.h"
#include "video_rxtx/ultragrid_rtp.h"

#include <iostream>
#include <mutex>
#include <memory>
#include <queue>
#include <thread>

static constexpr int VIDCAP_UG_INPUT_ID  = 0xa44b19ee;
static constexpr int MAX_QUEUE_SIZE = 2;

using namespace std;

struct ug_input_state  : public frame_recv_delegate {
        mutex lock;
        queue<struct video_frame *> frame_queue;
        struct display *display;

        void frame_arrived(struct video_frame *f);
        thread         receiver_thread;
        thread             display_thread;
        unique_ptr<ultragrid_rtp_video_rxtx> video_rxtx;

        virtual ~ug_input_state() {}
};

void ug_input_state::frame_arrived(struct video_frame *f)
{
        lock_guard<mutex> lk(lock);
        if (frame_queue.size() < MAX_QUEUE_SIZE) {
                frame_queue.push(f);
        } else {
                cerr << "[ug_input] Dropping frame!" << endl;
                vf_free(f);
        }
}

void *vidcap_ug_input_init(const struct vidcap_params *cap_params)
{
        if (strcmp("help", vidcap_params_get_fmt(cap_params)) == 0) {
                printf("Usage:\n");
                printf("\t-t ug_input:<port>\n");
                return &vidcap_init_noerr;
        }
        ug_input_state *s = new ug_input_state();

        char cfg[128] = "";
        snprintf(cfg, sizeof cfg, "pipe:%p", s);
        assert (initialize_video_display(vidcap_params_get_parent(cap_params), "proxy", cfg, 0, &s->display) == 0);

        map<string, param_u> params;

        // common
        params["parent"].ptr = vidcap_params_get_parent(cap_params);
        params["exporter"].ptr = NULL;
        params["compression"].ptr = (void *) "none";
        params["rxtx_mode"].i = MODE_RECEIVER;

        //RTP
        params["mtu"].i = 9000; // doesn't matter anyway...
        // should be localhost and RX TX ports the same (here dynamic) in order to work like a pipe
        params["receiver"].ptr = (void *) "localhost";
        if (isdigit(vidcap_params_get_fmt(cap_params)[0]))
                params["rx_port"].i = atoi(vidcap_params_get_fmt(cap_params));
        else
                params["rx_port"].i = 5004; // default
        params["tx_port"].i = 0;
        params["use_ipv6"].b = false;
        params["mcast_if"].ptr = (void *) NULL;
        params["fec"].ptr = (void *) "none";
        params["encryption"].ptr = (void *) NULL;
        params["packet_rate"].i = 0;

        // UltraGrid RTP
        params["postprocess"].ptr = (void *) NULL;
        params["decoder_mode"].l = VIDEO_NORMAL;
        params["display_device"].ptr = s->display;

        s->video_rxtx = unique_ptr<ultragrid_rtp_video_rxtx>(dynamic_cast<ultragrid_rtp_video_rxtx *>(video_rxtx::create(ULTRAGRID_RTP, params)));
        assert (s->video_rxtx);

        s->receiver_thread = thread(&video_rxtx::receiver_thread, s->video_rxtx.get());
        s->display_thread = thread(display_run, s->display);

        return s;
}

void vidcap_ug_input_done(void *state)
{
        auto s = (ug_input_state *) state;

        should_exit_receiver = true;
        s->receiver_thread.join();

        display_put_frame(s->display, NULL, 0);
        s->display_thread.join();
        display_done(s->display);

        s->video_rxtx->join();

        while (!s->frame_queue.empty()) {
                auto frame = s->frame_queue.front();
                s->frame_queue.pop();
                vf_free(frame);
        }

        delete s;
}

struct video_frame *vidcap_ug_input_grab(void *state, struct audio_frame **audio)
{
        auto s = (ug_input_state *) state;
        *audio = NULL;
        lock_guard<mutex> lk(s->lock);
        if (s->frame_queue.empty()) {
                return NULL;
        } else {
                auto frame = s->frame_queue.front();
                s->frame_queue.pop();
                frame->dispose = vf_free;
                return frame;
        }
}

struct vidcap_type *vidcap_ug_input_probe(bool /* verbose */)
{
        struct vidcap_type *vt;

        vt = (struct vidcap_type *) calloc(1, sizeof(struct vidcap_type));
        if (vt != NULL) {
                vt->id = VIDCAP_UG_INPUT_ID;
                vt->name = "ug_input";
                vt->description = "Dummy capture from UG received network";
        }
        return vt;
}
