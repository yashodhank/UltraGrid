/**
 * @file   video_rxtx/rtp.cpp
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


#ifdef HAVE_CONFIG_H
#include "config.h"
#include "config_unix.h"
#include "config_win32.h"
#endif // HAVE_CONFIG_H

#include "video_rxtx/rtp.h"

#include "debug.h"

#include <sstream>
#include <string>
#include <stdexcept>

#include "host.h"
#include "ihdtv.h"
#include "messaging.h"
#include "module.h"
#include "pdb.h"
#include "rtp/rtp.h"
#include "rtp/video_decoders.h"
#include "rtp/pbuf.h"
#include "tfrc.h"
#include "stats.h"
#include "transmit.h"
#include "tv.h"
#include "utils/vf_split.h"
#include "video.h"
#include "video_compress.h"
#include "video_decompress.h"
#include "video_display.h"
#include "video_export.h"
#include "video_rxtx.h"

using namespace std;

void rtp_video_rxtx::process_message(struct msg_sender *msg)
{
        int ret;
        switch(msg->type) {
                case SENDER_MSG_CHANGE_RECEIVER:
                        assert(m_connections_count == 1);
                        ret = rtp_change_dest(m_network_devices[0],
                                        msg->receiver);

                        if(ret == FALSE) {
                                fprintf(stderr, "Changing receiver to: %s failed!\n",
                                                msg->receiver);
                        }
                        break;
                case SENDER_MSG_CHANGE_PORT:
                        change_tx_port(msg->port);
                        break;
                case SENDER_MSG_PAUSE:
                        m_paused = true;
                        break;
                case SENDER_MSG_PLAY:
                        m_paused = false;
                        break;
        }
}

rtp_video_rxtx::rtp_video_rxtx(struct module *parent,
                struct video_export *video_exporter,
                const char *requested_compression, const char *requested_encryption,
                const char *receiver, int rx_port, int tx_port,
                bool use_ipv6, const char *mcast_if, const char *requested_video_fec,
                int requested_mtu, long packet_rate) :
        video_rxtx(parent, video_exporter, requested_compression)
{
        if(requested_mtu > RTP_MAX_PACKET_LEN) {
                ostringstream oss;
                oss << "Requested MTU exceeds maximal value allowed by RTP library (" <<
                        RTP_MAX_PACKET_LEN << ").";
                throw oss.str();
        }

        m_participants = pdb_init();
        m_requested_receiver = receiver;
        m_recv_port_number = rx_port;
        m_send_port_number = tx_port;
        m_ipv6 = use_ipv6;
        m_requested_mcast_if = mcast_if;
        
        if ((m_network_devices = initialize_network(receiver, rx_port, tx_port,
                                        m_participants, use_ipv6, mcast_if))
                        == NULL) {
                throw string("Unable to open network");
        } else {
                struct rtp **item;
                m_connections_count = 0;
                /* only count how many connections has initialize_network opened */
                for(item = m_network_devices; *item != NULL; ++item)
                        ++m_connections_count;
        }

        if ((m_tx = tx_init(&m_sender_mod,
                                        requested_mtu, TX_MEDIA_VIDEO,
                                        requested_video_fec,
                                        requested_encryption, packet_rate)) == NULL) {
                throw string("Unable to initialize transmitter");
        }
}

rtp_video_rxtx::~rtp_video_rxtx()
{
        if (m_tx) {
                module_done(CAST_MODULE(m_tx));
        }

        if (m_participants != NULL) {
                pdb_iter_t it;
                struct pdb_e *cp = pdb_iter_init(m_participants, &it);
                while (cp != NULL) {
                        struct pdb_e *item = NULL;
                        pdb_remove(m_participants, cp->ssrc, &item);
                        cp = pdb_iter_next(&it);
                        free(item);
                }
                pdb_iter_done(&it);
                pdb_destroy(&m_participants);
        }
}

void rtp_video_rxtx::change_tx_port(int tx_port)
{
        destroy_rtp_devices(m_network_devices);
        m_send_port_number = tx_port;
        m_network_devices = initialize_network(m_requested_receiver, m_recv_port_number,
                        m_send_port_number, m_participants, m_ipv6,
                        m_requested_mcast_if);
        if (!m_network_devices) {
                throw string("Changing RX port failed!\n");
        }
}

