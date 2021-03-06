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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

// #define DUMP_WRITE
// #define DUMP_OUT_PACKETS

int bgav_stream_start(bgav_stream_t * stream)
  {
  int result = 1;
  
  switch(stream->type)
    {
    case BGAV_STREAM_VIDEO:
      result = bgav_video_start(stream);
      break;
    case BGAV_STREAM_AUDIO:
      result = bgav_audio_start(stream);
      break;
    case BGAV_STREAM_SUBTITLE_OVERLAY:
    case BGAV_STREAM_SUBTITLE_TEXT:
      result = bgav_subtitle_start(stream);
      break;
    default:
      break;
    }
  
  if(result)
    stream->initialized = 1;
  return result;
  }

void bgav_stream_stop(bgav_stream_t * s)
  {
  if((s->action == BGAV_STREAM_DECODE) ||
     (s->action == BGAV_STREAM_PARSE) ||
     (s->action == BGAV_STREAM_READRAW))
    {
    switch(s->type)
      {
      case BGAV_STREAM_VIDEO:
        bgav_video_stop(s);
        break;
      case BGAV_STREAM_AUDIO:
        bgav_audio_stop(s);
        break;
      case BGAV_STREAM_SUBTITLE_TEXT:
      case BGAV_STREAM_SUBTITLE_OVERLAY:
        bgav_subtitle_stop(s);
      default:
        break;
      }
    }
  if(s->packet_buffer)
    bgav_packet_buffer_clear(s->packet_buffer);

  /* Clear eventually stored packets */
  if(s->packet)
    {
    bgav_packet_pool_put(s->pp, s->packet);
    s->packet = NULL;
    }
  
  s->index_position = s->first_index_position;
  s->in_position = 0;
  s->out_time = 0;
  s->packet_seq = 0;

  s->flags &= ~(STREAM_EOF_C|STREAM_EOF_D);
  
  STREAM_UNSET_SYNC(s);
  
  }

void bgav_stream_create_packet_buffer(bgav_stream_t * stream)
  {
  stream->packet_buffer = bgav_packet_buffer_create(stream->pp);
  }

void bgav_stream_init(bgav_stream_t * stream, const bgav_options_t * opt)
  {
  memset(stream, 0, sizeof(*stream));
  STREAM_UNSET_SYNC(stream);
  stream->first_index_position = INT_MAX;

  /* need to set this to -1 so we know, if this stream has packets at all */
  stream->last_index_position = -1; 
  stream->index_position = -1;
  stream->opt = opt;
  stream->pp = bgav_packet_pool_create();
  }

void bgav_stream_free(bgav_stream_t * s)
  {
  /* Cleanup must be called as long as the other
     members are still functional */
  if(s->cleanup)
    s->cleanup(s);
  
  if(s->ext_data)
    free(s->ext_data);
  
  if(s->file_index)
    bgav_file_index_destroy(s->file_index);

  gavl_metadata_free(&s->m);
  
  if(s->packet_buffer)
    bgav_packet_buffer_destroy(s->packet_buffer);

  if(((s->type == BGAV_STREAM_SUBTITLE_TEXT) ||
      (s->type == BGAV_STREAM_SUBTITLE_OVERLAY)) &&
     s->data.subtitle.subreader)
    bgav_subtitle_reader_destroy(s);

  if(s->type == BGAV_STREAM_VIDEO)
    {
    if(s->data.video.pal.entries)
      free(s->data.video.pal.entries);
    }
  
  if(s->timecode_table)
    bgav_timecode_table_destroy(s->timecode_table);
  if(s->pp)
    bgav_packet_pool_destroy(s->pp);
  }

void bgav_stream_dump(bgav_stream_t * s)
  {
  switch(s->type)
    {
    case BGAV_STREAM_AUDIO:
      bgav_dprintf("============ Audio stream ============\n");
      break;
    case BGAV_STREAM_VIDEO:
      bgav_dprintf("============ Video stream ============\n");
      break;
    case BGAV_STREAM_SUBTITLE_TEXT:
      bgav_dprintf("=========== Text subtitles ===========\n");
      break;
    case BGAV_STREAM_SUBTITLE_OVERLAY:
      bgav_dprintf("========= Overlay subtitles ===========\n");
      break;
    case BGAV_STREAM_UNKNOWN:
      return;
    }

  bgav_dprintf("  Metadata:\n");
  gavl_metadata_dump(&s->m, 4);
  
  bgav_dprintf("  Fourcc:            ");
  bgav_dump_fourcc(s->fourcc);
  bgav_dprintf("\n");
 
  bgav_dprintf("  Stream ID:         %d (0x%x)\n",
          s->stream_id,
          s->stream_id);
  bgav_dprintf("  Codec bitrate:     ");
  if(s->codec_bitrate == BGAV_BITRATE_VBR)
    bgav_dprintf("Variable\n");
  else if(s->codec_bitrate)
    bgav_dprintf("%d\n", s->codec_bitrate);
  else
    bgav_dprintf("Unspecified\n");

  bgav_dprintf("  Container bitrate: ");
  if(s->container_bitrate == BGAV_BITRATE_VBR)
    bgav_dprintf("Variable\n");
  else if(s->container_bitrate)
    bgav_dprintf("%d\n", s->container_bitrate);
  else
    bgav_dprintf("Unspecified\n");

  bgav_dprintf("  Timescale:         %d\n", s->timescale);
  bgav_dprintf("  Duration:          %"PRId64"\n", s->duration);
  // bgav_dprintf("  Private data:      %p\n", s->priv);
  bgav_dprintf("  Codec header:      %d bytes\n", s->ext_size);
  }

void bgav_stream_clear(bgav_stream_t * s)
  {
  if(s->packet_buffer)
    bgav_packet_buffer_clear(s->packet_buffer);
  if(s->packet)
    {
    bgav_packet_pool_put(s->pp, s->packet);
    s->packet = NULL;
    }
  s->in_position  = 0;
  s->out_time = BGAV_TIMESTAMP_UNDEFINED;
  STREAM_UNSET_SYNC(s);
  s->flags &= ~(STREAM_EOF_C|STREAM_EOF_D);
  }

int bgav_stream_skipto(bgav_stream_t * s, gavl_time_t * time, int scale)
  {
  if(s->action != BGAV_STREAM_DECODE)
    return 1;
  
  switch(s->type)
    {
    case BGAV_STREAM_AUDIO:
      return bgav_audio_skipto(s, time, scale);
      break;
    case BGAV_STREAM_VIDEO:
      return bgav_video_skipto(s, time, scale, 1);
      break;
    case BGAV_STREAM_SUBTITLE_TEXT:
    case BGAV_STREAM_SUBTITLE_OVERLAY:
      return bgav_subtitle_skipto(s, time, scale);
      break;
    case BGAV_STREAM_UNKNOWN:
      break;
    }
  return 0;
  }

bgav_packet_t * bgav_stream_get_packet_write(bgav_stream_t * s)
  {
  return bgav_packet_pool_get(s->pp);
  }

void bgav_stream_done_packet_write(bgav_stream_t * s, bgav_packet_t * p)
  {
#ifdef DUMP_WRITE
  bgav_dprintf(stderr, "bgav_stream_done_packet_write ");
  bgav_packet_dump(p);
#endif
  s->in_position++;

  /* If the stream has a constant framerate, all packets have the same
     duration */
  if(s->type == BGAV_STREAM_VIDEO)
    {
    if((s->data.video.format.frame_duration) &&
       (s->data.video.format.framerate_mode == GAVL_FRAMERATE_CONSTANT) &&
       !p->duration)
      p->duration = s->data.video.format.frame_duration;

    if(s->data.video.pal.size && !s->data.video.pal.sent)
      {
      bgav_packet_alloc_palette(p, s->data.video.pal.size);
      memcpy(p->palette, s->data.video.pal.entries,
             s->data.video.pal.size * sizeof(*p->palette));
      s->data.video.pal.sent = 1;
      }
    }
  /* Padding (if fourcc != gavl) */
  if(p->data)
    memset(p->data + p->data_size, 0, PACKET_PADDING);

  /* Set timestamps from file index because the
     demuxer might have them messed up */
  if((s->action != BGAV_STREAM_PARSE) && s->file_index)
    {
    p->position = s->index_position++;
    }
  
  bgav_packet_buffer_append(s->packet_buffer, p);
  }

int bgav_stream_get_index(bgav_stream_t * s)
  {
  switch(s->type)
    {
    case BGAV_STREAM_AUDIO:
      return (int)(s - s->track->audio_streams);
      break;
    case BGAV_STREAM_VIDEO:
      return (int)(s - s->track->video_streams);
      break;
    case BGAV_STREAM_SUBTITLE_TEXT:
    case BGAV_STREAM_SUBTITLE_OVERLAY:
      return (int)(s - s->track->subtitle_streams);
      break;
    case BGAV_STREAM_UNKNOWN:
      break;
    }
  return -1;
  }

bgav_packet_t *
bgav_stream_get_packet_read(bgav_stream_t * s)
  {
#ifdef DUMP_OUT_PACKETS
  bgav_packet_t * p;
  p = s->src.get_func(s->src.data);
  if(p)
    {
    bgav_dprintf("Got packet (stream %d): ", s->stream_id);
    bgav_packet_dump(p);
    }
  return p;
#else
  return s->src.get_func(s->src.data);
#endif
  }

bgav_packet_t *
bgav_stream_peek_packet_read(bgav_stream_t * s, int force)
  {
  return s->src.peek_func(s->src.data, force);
  }

void bgav_stream_done_packet_read(bgav_stream_t * s, bgav_packet_t * p)
  {
  bgav_packet_pool_put(s->pp, p);
  }

void bgav_stream_set_extradata(bgav_stream_t * s, const uint8_t * data, int len)
  {
  if(len <= 0)
    return;
  
  s->ext_size = len;
  s->ext_data = malloc(len + 16);
  memcpy(s->ext_data, data, len);
  memset(s->ext_data + len, 0, 16);
  }
