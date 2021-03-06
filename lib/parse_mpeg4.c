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

#include <string.h>
#include <stdlib.h>


#include <avdec_private.h>
#include <parser.h>
#include <videoparser_priv.h>

#include <mpeg4_header.h>
#include <mpv_header.h>

#define LOG_DOMAIN "parse_mpeg4"

// #define DUMP_HEADERS

#define MPEG4_NEED_SYNC                   0
#define MPEG4_NEED_STARTCODE              1
#define MPEG4_HAS_VOL_CODE                2
#define MPEG4_HAS_VOL_HEADER              3
#define MPEG4_HAS_VOP_CODE                4
#define MPEG4_HAS_VOP_HEADER              5
#define MPEG4_HAS_USER_DATA_CODE          6

typedef struct
  {
  /* Sequence header */
  bgav_mpeg4_vol_header_t vol;
  int have_vol;
  int has_picture_start;
  int state;

  char * user_data;
  int user_data_size;

  /* Save frames for packed B-frames */

  bgav_packet_t * saved_packet;

#if 0  
  uint8_t * saved_frame;
  int saved_frame_alloc;
  int saved_frame_size;
  int saved_frame_type;
  int saved_frame_pos;
#endif
  int packed_b_frames;
  } mpeg4_priv_t;

static void set_format(bgav_video_parser_t * parser)
  {
  mpeg4_priv_t * priv = parser->priv;

  if(!parser->format->timescale || !parser->format->frame_duration)
    {
    parser->format->timescale = priv->vol.vop_time_increment_resolution;
    parser->format->frame_duration = priv->vol.fixed_vop_time_increment;
    bgav_video_parser_set_framerate(parser);
    }
  
#if 0
  parser->format.image_width  = priv->sh.horizontal_size_value;
  parser->format.image_height = priv->sh.vertical_size_value;
  parser->format.frame_width  =
    (parser->format.image_width + 15) & ~15;
  parser->format.frame_height  =
    (parser->format.image_height + 15) & ~15;
#endif
  if(priv->vol.low_delay)
    parser->s->flags &= ~(STREAM_B_FRAMES|STREAM_WRONG_B_TIMESTAMPS);
  }

static void reset_mpeg4(bgav_video_parser_t * parser)
  {
  mpeg4_priv_t * priv = parser->priv;
  priv->state = MPEG4_NEED_SYNC;
  priv->has_picture_start = 0;
  priv->saved_packet->data_size = 0;
  }

static int extract_user_data(bgav_video_parser_t * parser,
                             const uint8_t * data, const uint8_t * data_end)
  {
  int i;
  int is_vendor;
  mpeg4_priv_t * priv = parser->priv;
  const uint8_t * pos1;
  if(priv->user_data)
    return 4;

  pos1 = data+4;
  pos1 = bgav_mpv_find_startcode(pos1, data_end);
  
  if(pos1)
    priv->user_data_size = pos1 - (data + 4);
  else
    priv->user_data_size = (data_end - data) - 4;
  
  priv->user_data = calloc(1, priv->user_data_size+1);
  memcpy(priv->user_data, data+4, priv->user_data_size);

#ifdef DUMP_HEADERS
  bgav_dprintf("Got user data\n");
  bgav_hexdump((uint8_t*)priv->user_data, priv->user_data_size, 16);
#endif

  if(!strncasecmp(priv->user_data, "divx", 4) &&
     (priv->user_data[priv->user_data_size-1] == 'p'))
    {
    bgav_log(parser->s->opt, BGAV_LOG_INFO, LOG_DOMAIN,
             "Detected packed B-frames");
    priv->packed_b_frames = 1;
    }

  /* Set software field in metadata */
  is_vendor = 1;
  for(i = 0; i < priv->user_data_size - priv->packed_b_frames; i++)
    {
    if(((uint8_t)priv->user_data[i] < 32) ||
       ((uint8_t)priv->user_data[i] > 127))
      {
      is_vendor = 0;
      break;
      }
    }
  if(is_vendor)
    {
    int vendor_len = priv->user_data_size - priv->packed_b_frames;
    char * vendor = malloc(vendor_len + 1);
    memcpy(vendor, priv->user_data, vendor_len);
    vendor[vendor_len] = '\0';
    gavl_metadata_set_nocpy(&parser->s->m, GAVL_META_SOFTWARE,
                            vendor);
    }
  
  return priv->user_data_size+4;
  }

static int parse_header_mpeg4(bgav_video_parser_t * parser)
  {
  mpeg4_priv_t * priv = parser->priv;
  const uint8_t * pos = parser->s->ext_data;
  int len;
  while(1)
    {
    pos = bgav_mpv_find_startcode(pos, parser->s->ext_data +
                                  parser->s->ext_size);
    if(!pos)
      return priv->have_vol;
    
    switch(bgav_mpeg4_get_start_code(pos))
      {
      case MPEG4_CODE_VOL_START:
        len = bgav_mpeg4_vol_header_read(parser->s->opt,
                                         &priv->vol, pos,
                                         parser->s->ext_size -
                                         (pos - parser->s->ext_data));
        if(!len)
          return 0;
        priv->have_vol = 1;
#ifdef DUMP_HEADERS
        bgav_mpeg4_vol_header_dump(&priv->vol);
#endif
        set_format(parser);
        pos += len;
        break;
      case MPEG4_CODE_USER_DATA:
        pos += extract_user_data(parser, pos, parser->s->ext_data +
                                 parser->s->ext_size);
        break;
      default:
        pos += 4;
        break;
      }
    }
  return 0;
  }

static int parse_mpeg4(bgav_video_parser_t * parser)
  {
  const uint8_t * sc;
  int len;
  mpeg4_priv_t * priv = parser->priv;
  bgav_mpeg4_vop_header_t vh;
  const uint8_t * end_pos;
  int start_code;
  
  switch(priv->state)
    {
    case MPEG4_NEED_SYNC:
      sc = bgav_mpv_find_startcode(parser->buf.buffer + parser->pos,
                                   parser->buf.buffer + parser->buf.size);
      if(!sc)
        return PARSER_NEED_DATA;
      bgav_video_parser_flush(parser, sc - parser->buf.buffer);
      parser->pos = 0;
      priv->state = MPEG4_NEED_STARTCODE;
      break;
    case MPEG4_NEED_STARTCODE:
      sc = bgav_mpv_find_startcode(parser->buf.buffer + parser->pos,
                                   parser->buf.buffer + parser->buf.size);
      if(!sc)
        return PARSER_NEED_DATA;
      
      start_code = bgav_mpeg4_get_start_code(sc);
      parser->pos = sc - parser->buf.buffer;

      switch(start_code)
        {
        case MPEG4_CODE_VO_START:
          if(!priv->has_picture_start)
            {
            if(!bgav_video_parser_set_picture_start(parser))
              return PARSER_ERROR;
            priv->has_picture_start = 1;
            }
          priv->state = MPEG4_NEED_STARTCODE;
          parser->pos+=4;
          break;
        case MPEG4_CODE_VOL_START:
          if(!priv->has_picture_start)
            {
            if(!bgav_video_parser_set_picture_start(parser))
              return PARSER_ERROR;
            priv->has_picture_start = 1;
            }
          if(!priv->have_vol)
            priv->state = MPEG4_HAS_VOL_CODE;
          else
            {
            priv->state = MPEG4_NEED_STARTCODE;
            parser->pos+=4;
            }
          break;
        case MPEG4_CODE_VOP_START:
          /* TODO: Skip pictures before the first VOL header */
          
          if(!priv->has_picture_start)
            {
            if(!bgav_video_parser_set_picture_start(parser))
              return PARSER_ERROR;
            }
          priv->has_picture_start = 0;
          
          bgav_video_parser_set_header_end(parser);
          
          /* Need the picture header */
          priv->state = MPEG4_HAS_VOP_CODE;
          
          if(!parser->s->ext_data)
            {
            bgav_video_parser_extract_header(parser);
            return PARSER_CONTINUE;
            }
          break;
        case MPEG4_CODE_USER_DATA:
          if(!priv->user_data)
            priv->state = MPEG4_HAS_USER_DATA_CODE;
          else
            {
            /* Ignore repeated user data */
            parser->pos += 4;
            priv->state = MPEG4_NEED_STARTCODE;
            }
          break;
        default:
          parser->pos += 4;
          priv->state = MPEG4_NEED_STARTCODE;
          break;
        }
      
      break;
    case MPEG4_HAS_VOP_CODE:
      /* Try to get the picture header */
      
      len = bgav_mpeg4_vop_header_read(parser->s->opt,
                                        &vh,
                                        parser->buf.buffer + parser->pos,
                                        parser->buf.size - parser->pos, &priv->vol);
      if(!len)
        return PARSER_NEED_DATA;
#ifdef DUMP_HEADERS
      bgav_mpeg4_vop_header_dump(&vh);
#endif      
      bgav_video_parser_set_coding_type(parser, vh.coding_type);
      
      //        fprintf(stderr, "Pic type: %c\n", ph.coding_type);
      parser->pos += len;
      priv->state = MPEG4_NEED_STARTCODE;
      
      break;
      //    case MPEG_GOP_CODE:
      //      break;
    case MPEG4_HAS_VOL_CODE:
      /* Try to get the sequence header */

      if(!priv->have_vol)
        {
        len = bgav_mpeg4_vol_header_read(parser->s->opt,
                                         &priv->vol,
                                         parser->buf.buffer + parser->pos,
                                         parser->buf.size - parser->pos);
        if(!len)
          return PARSER_NEED_DATA;
#ifdef DUMP_HEADERS
        bgav_mpeg4_vol_header_dump(&priv->vol);
#endif        
        parser->pos += len;

        set_format(parser);
        
        priv->have_vol = 1;
        }
      else
        parser->pos += 4;
      
      priv->state = MPEG4_NEED_STARTCODE;
      
      break;
    case MPEG4_HAS_USER_DATA_CODE:
      end_pos =
        bgav_mpv_find_startcode(parser->buf.buffer + parser->pos + 4,
                                parser->buf.buffer + parser->buf.size - 4);
      if(!end_pos)
        return PARSER_NEED_DATA;

      priv->user_data_size =
        end_pos - (parser->buf.buffer + parser->pos + 4);
      priv->user_data = calloc(1, priv->user_data_size+1);
      memcpy(priv->user_data, parser->buf.buffer + parser->pos + 4,
             priv->user_data_size);
      parser->pos += (4 + priv->user_data_size);
      priv->state = MPEG4_NEED_STARTCODE;
      
      break;
    }
  return PARSER_CONTINUE;
  }

static void set_header_end(bgav_video_parser_t * parser, bgav_packet_t * p,
                           int pos)
  {
  if(p->header_size)
    return;
  
  if(!parser->s->ext_data)
    {
    parser->s->ext_size = pos;
    parser->s->ext_data = malloc(parser->s->ext_size);
    memcpy(parser->s->ext_data, p->data, parser->s->ext_size);
    }
  p->header_size = pos;
  }

#define SWAP(n1, n2) \
  swp = n1; n1 = n2; n2 = swp;

static int parse_frame_mpeg4(bgav_video_parser_t * parser, bgav_packet_t * p)
  {
  mpeg4_priv_t * priv = parser->priv;
  const uint8_t * data;
  uint8_t * data_end;
  int sc;
  int done = 0;
  int result;
  bgav_mpeg4_vop_header_t vh;
  int num_pictures = 0;
  
  data = p->data;
  data_end = p->data + p->data_size;

  while(!done)
    {
    data = bgav_mpv_find_startcode(data, data_end);

    if(!data)
      break;
    
    sc = bgav_mpeg4_get_start_code(data);

    switch(sc)
      {
      case MPEG4_CODE_VO_START:
        data += 4;
        break;
      case MPEG4_CODE_VOL_START:
        if(!priv->have_vol)
          {
          result = bgav_mpeg4_vol_header_read(parser->s->opt,
                                              &priv->vol, data,
                                              data_end - data);
          if(!result)
            return PARSER_ERROR;
          set_format(parser);
          data += result;

#ifdef DUMP_HEADERS
          bgav_mpeg4_vol_header_dump(&priv->vol);
#endif

          }
        else
          data += 4;
        break;
      case MPEG4_CODE_VOP_START:
        result = bgav_mpeg4_vop_header_read(parser->s->opt,
                                            &vh, data, data_end-data,
                                            &priv->vol);
        if(!result)
          return PARSER_ERROR;
#ifdef DUMP_HEADERS
        bgav_mpeg4_vop_header_dump(&vh);
#endif

        /* Check whether to copy a saved frame back */
        if(priv->saved_packet->data_size)
          {
          if(!vh.vop_coded)
            {
            /* Copy stuff back, overwriting the packet */
            bgav_packet_alloc(p, priv->saved_packet->data_size);
            memcpy(p->data, priv->saved_packet->data,
                   priv->saved_packet->data_size);
            p->data_size = priv->saved_packet->data_size;
            priv->saved_packet->data_size = 0;
            p->flags = priv->saved_packet->flags;
            p->position = priv->saved_packet->position;
            }
          else
            {
            int64_t swp;
            /* Output saved frame but save this one */
            bgav_packet_swap_data(priv->saved_packet, p);
            p->flags = priv->saved_packet->flags;
            SWAP(priv->saved_packet->position, p->position);
            PACKET_SET_CODING_TYPE(priv->saved_packet, vh.coding_type);
            
            }
          done = 1;
          }
        /* save this frame for later use */
        else if(priv->packed_b_frames && (num_pictures == 1))
          {
          bgav_packet_reset(priv->saved_packet);
          priv->saved_packet->data_size = data_end - data;
          bgav_packet_alloc(priv->saved_packet,
                            priv->saved_packet->data_size);
          memcpy(priv->saved_packet->data, data,
                 priv->saved_packet->data_size);
          PACKET_SET_CODING_TYPE(priv->saved_packet, vh.coding_type);
          priv->saved_packet->position = p->position;
          p->data_size -= priv->saved_packet->data_size;
          done = 1;
          num_pictures++;
          }
        else
          {
          set_header_end(parser, p, data - p->data);
          
          PACKET_SET_CODING_TYPE(p, vh.coding_type);
          data += result;
          
          if(p->header_size && priv->packed_b_frames)
            bgav_mpeg4_remove_packed_flag(p->data,
                                          &p->data_size, &p->header_size);
          num_pictures++;
          }
        if(!priv->packed_b_frames || (num_pictures == 2))
          done = 1;
        break;
      case MPEG4_CODE_GOV_START:
        set_header_end(parser, p, data - p->data);
        data += 4;
        break;
      case MPEG4_CODE_USER_DATA:
        result = extract_user_data(parser, data, data_end);
        data += result;
        break;
      default:
        data += 4;
        break;
      }
    }
  return PARSER_HAVE_PACKET;
  }

static void cleanup_mpeg4(bgav_video_parser_t * parser)
  {
  mpeg4_priv_t * priv = parser->priv;
  if(priv->user_data)
    free(priv->user_data);
  if(priv->saved_packet)
    bgav_packet_destroy(priv->saved_packet);
  free(parser->priv);
  }


void bgav_video_parser_init_mpeg4(bgav_video_parser_t * parser)
  {
  mpeg4_priv_t * priv;
  priv = calloc(1, sizeof(*priv));
  
  parser->priv = priv;
  priv->saved_packet = bgav_packet_create();
  
  if(parser->s->ext_data)
    parse_header_mpeg4(parser);
  
  parser->parse = parse_mpeg4;
  parser->parse_frame = parse_frame_mpeg4;
  parser->cleanup = cleanup_mpeg4;
  parser->reset = reset_mpeg4;

  }
