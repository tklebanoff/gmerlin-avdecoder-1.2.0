/*****************************************************************
 * gmerlin-avdecoder - a general purpose multimedia decoding library
 *
 * Copyright (c) 2001 - 2011 Members of the Gmerlin project
 * gmerlin-general@lists.sourceforge.net
 * http://gmerlin.sourceforge.net
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * *****************************************************************/


#include <avdec_private.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <mms.h>

#define LOG_DOMAIN "in_mms"

typedef struct
  {
  bgav_mms_t * mms;

  int buffer_size;
  uint8_t * buffer_ptr;
  uint8_t * buffer;
  
  } mms_priv_t;

extern bgav_demuxer_t bgav_demuxer_asf;

static int open_mms(bgav_input_context_t * ctx, const char * url, char ** r)
  {
  uint8_t * header;
  int header_len;
  int num_streams;
  int * stream_ids;
  int i;
  bgav_input_context_t * input;
  bgav_track_t * track;
  const char * title;
  mms_priv_t * priv;

  priv = calloc(1, sizeof(*priv));
  ctx->priv = priv;

  /* Open mms connection */
  
  priv->mms = bgav_mms_open(ctx->opt, url);
  if(!priv->mms)
    goto fail;

  /* Get Header and initialize demultiplexer */

  header = bgav_mms_get_header(priv->mms, &header_len);

  if(!header)
    goto fail;

  input = bgav_input_open_memory(header, header_len, ctx->opt);

  ctx->demuxer = bgav_demuxer_create(ctx->opt, &bgav_demuxer_asf,
                                     input);

  if(!bgav_demuxer_start(ctx->demuxer, NULL))
    {
    bgav_log(ctx->opt, BGAV_LOG_ERROR, LOG_DOMAIN,
             "Initializing asf demuxer failed");
    return 0;
    }

  /* Check, what streams we have */
  
  track = ctx->demuxer->tt->cur;
    
  num_streams = track->num_audio_streams + track->num_video_streams;
  
  stream_ids = malloc(num_streams * sizeof(int));
  
  for(i = 0; i < track->num_audio_streams; i++)
    stream_ids[i] = track->audio_streams[i].stream_id;

  for(i = 0; i < track->num_video_streams; i++)
    stream_ids[i + track->num_audio_streams] =
      track->video_streams[i].stream_id;
  
  bgav_mms_select_streams(priv->mms, stream_ids, num_streams);

  title = gavl_metadata_get(&track->metadata, GAVL_META_TITLE);
  if(!track->name && title)
    track->name = bgav_strdup(title);
  
  free(stream_ids);
  /* Set the input context of the demuxer */
  ctx->do_buffer = 1;
  ctx->demuxer->input = ctx;
  ctx->position = header_len;
  bgav_input_destroy(input);
  /* The demuxer might think he can seek, but that's  not true */

  ctx->demuxer->flags &= ~BGAV_DEMUXER_CAN_SEEK;
  
  ctx->url = bgav_strdup(url);
  
  return 1;
  
  fail:
  return 0;
  }

#define __MIN(a, b) (((a)<(b))?(a):(b))

static int do_read(bgav_input_context_t* ctx,
                   uint8_t * buffer, int len, int block)
  {
  mms_priv_t * priv;
  //  int buffer_size;
  //  uint8_t * buffer_ptr;
  //  uint8_t * buffer;
  int bytes_to_copy;
  int bytes_read;

  priv = ctx->priv;
  
  bytes_read = 0;
  while(bytes_read < len)
    {
    if(!priv->buffer_size)
      {
      priv->buffer = bgav_mms_read_data(priv->mms, &priv->buffer_size, block);
      if(!priv->buffer)
        return bytes_read;
      priv->buffer_ptr = priv->buffer;
      }

    bytes_to_copy = __MIN(len - bytes_read, priv->buffer_size);
    memcpy(buffer + bytes_read, priv->buffer_ptr, bytes_to_copy);
    priv->buffer_ptr += bytes_to_copy;
    priv->buffer_size -= bytes_to_copy;
    bytes_read += bytes_to_copy;
    }
  
  return bytes_read; 
  }

static int read_mms(bgav_input_context_t* ctx,
                    uint8_t * buffer, int len)
  {
  return do_read(ctx, buffer, len, 1);

  }

static int read_mms_nonblock(bgav_input_context_t* ctx,
                             uint8_t * buffer, int len)
  {
  return do_read(ctx, buffer, len, 0);

  }

static void    close_mms(bgav_input_context_t * ctx)
  {
  mms_priv_t * mms;
  mms = ctx->priv;
  if(mms->mms)
    bgav_mms_close(mms->mms);
  free(mms);
  }


const bgav_input_t bgav_input_mms =
  {
    .name =          "mms (Windows Media)",
    .open =          open_mms,
    .read =          read_mms,
    .read_nonblock = read_mms_nonblock,
    //    .seek_byte = seek_byte_mms,
    .close =         close_mms
  };

