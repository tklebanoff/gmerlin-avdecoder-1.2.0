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

#define _GNU_SOURCE /* vasprintf */

#include <avdec_private.h>

#define LOG_DOMAIN "utils"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>
#include <regex.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <pthread.h>
#else
#include <sys/socket.h>
#include <sys/select.h>
#endif

#include <utils.h>

void bgav_dump_fourcc(uint32_t fourcc)
  {
  if((fourcc & 0xffff0000) || !(fourcc))
    bgav_dprintf( "%c%c%c%c (%08x)",
            (fourcc & 0xFF000000) >> 24,
            (fourcc & 0x00FF0000) >> 16,
            (fourcc & 0x0000FF00) >> 8,
            (fourcc & 0x000000FF),
            fourcc);
  else
    bgav_dprintf( "WavID: 0x%04x", fourcc);
    
  }

void bgav_hexdump(const uint8_t * data, int len, int linebreak)
  {
  int i;
  int bytes_written = 0;
  int imax;
  
  while(bytes_written < len)
    {
    imax = (bytes_written + linebreak > len) ? len - bytes_written : linebreak;
    for(i = 0; i < imax; i++)
      bgav_dprintf( "%02x ", data[bytes_written + i]);
    for(i = imax; i < linebreak; i++)
      bgav_dprintf( "   ");
    for(i = 0; i < imax; i++)
      {
      if((data[bytes_written + i] < 0x7f) && (data[bytes_written + i] >= 32))
        bgav_dprintf( "%c", data[bytes_written + i]);
      else
        bgav_dprintf( ".");
      }
    bytes_written += imax;
    bgav_dprintf( "\n");
    }
  }

char * bgav_sprintf(const char * format,...)
  {
  va_list argp; /* arg ptr */
#ifndef HAVE_VASPRINTF
  int len;
#endif
  char * ret;
  va_start( argp, format);

#ifndef HAVE_VASPRINTF
  len = vsnprintf(NULL, 0, format, argp);
  ret = malloc(len+1);
  vsnprintf(ret, len+1, format, argp);
#else
  vasprintf(&ret, format, argp);
#endif
  va_end(argp);
  return ret;
  }

void bgav_dprintf(const char * format, ...)
  {
  va_list argp; /* arg ptr */
  va_start( argp, format);
  vfprintf(stderr, format, argp);
  va_end(argp);
  }

void bgav_diprintf(int indent, const char * format, ...)
  {
  int i;
  va_list argp; /* arg ptr */
  for(i = 0; i < indent; i++)
    bgav_dprintf( " ");
  
  va_start( argp, format);
  vfprintf(stderr, format, argp);
  va_end(argp);
  }



char * bgav_strndup(const char * start, const char * end)
  {
  char * ret;
  int len;

  if(!start)
    return NULL;

  len = (end) ? (end - start) : strlen(start);
  ret = malloc(len+1);
  strncpy(ret, start, len);
  ret[len] = '\0';
  return ret;
  }

char * bgav_strdup(const char * str)
  {
  return (bgav_strndup(str, NULL));
  }

char * bgav_strncat(char * old, const char * start, const char * end)
  {
  int len, old_len;
  old_len = old ? strlen(old) : 0;
  
  len = (end) ? (end - start) : strlen(start);
  old = realloc(old, len + old_len + 1);
  strncpy(old + old_len, start, len);
  old[old_len + len] = '\0';
  return old;
  }

static char * remove_spaces(char * old)
  {
  char * pos1, *pos2, *ret;
  int num_spaces = 0;

  pos1 = old;
  while(*pos1 != '\0')
    {
    if(*pos1 == ' ')
      num_spaces++;
    pos1++;
    }
  if(!num_spaces)
    return old;
  
  ret = malloc(strlen(old) + 1 + 2 * num_spaces);

  pos1 = old;
  pos2 = ret;

  while(*pos1 != '\0')
    {
    if(*pos1 == ' ')
      {
      *(pos2++) = '%';
      *(pos2++) = '2';
      *(pos2++) = '0';
      }
    else
      *(pos2++) = *pos1;
    pos1++;
    }
  *pos2 = '\0';
  free(old);
  return ret;
  }

/* Split an URL, returned pointers should be free()d after */

int bgav_url_split(const char * url,
                   char ** protocol,
                   char ** user,
                   char ** password,
                   char ** hostname,
                   int * port,
                   char ** path)
  {
  const char * pos1;
  const char * pos2;

  /* For detecting user:pass@blabla.com/file */

  const char * colon_pos;
  const char * at_pos;
  const char * slash_pos;
  
  pos1 = url;

  /* Sanity check */
  
  pos2 = strstr(url, "://");
  if(!pos2)
    return 0;

  /* Protocol */
    
  if(protocol)
    *protocol = bgav_strndup(pos1, pos2);

  pos2 += 3;
  pos1 = pos2;

  /* Check for user and password */

  colon_pos = strchr(pos1, ':');
  at_pos = strchr(pos1, '@');
  slash_pos = strchr(pos1, '/');

  if(colon_pos && at_pos && at_pos &&
     (colon_pos < at_pos) && 
     (at_pos < slash_pos))
    {
    if(user)
      *user = bgav_strndup(pos1, colon_pos);
    pos1 = colon_pos + 1;
    if(password)
      *password = bgav_strndup(pos1, at_pos);
    pos1 = at_pos + 1;
    pos2 = pos1;
    }
  
  /* Hostname */

  while((*pos2 != '\0') && (*pos2 != ':') && (*pos2 != '/'))
    pos2++;

  if(hostname)
    *hostname = bgav_strndup(pos1, pos2);

  switch(*pos2)
    {
    case '\0':
      if(port)
        *port = -1;
      return 1;
      break;
    case ':':
      /* Port */
      pos2++;
      if(port)
        *port = atoi(pos2);
      while(isdigit(*pos2))
        pos2++;
      break;
    default:
      if(port)
        *port = -1;
      break;
    }

  if(path)
    {
    pos1 = pos2;
    pos2 = pos1 + strlen(pos1);
    if(pos1 != pos2)
      *path = bgav_strndup(pos1, pos2);
    else
      *path = NULL;
    }

  /* Fix whitespaces in path */
  if(path && *path)
    *path = remove_spaces(*path);
  return 1;
  }

/*
 *  Read a single line from a filedescriptor
 *
 *  ret will be reallocated if neccesary and ret_alloc will
 *  be updated then
 *
 *  The string will be 0 terminated, a trailing \r or \n will
 *  be removed
 */
#define BYTES_TO_ALLOC 1024

int bgav_read_line_fd(const bgav_options_t * opt,
                      int fd, char ** ret, int * ret_alloc, int milliseconds)
  {
  char * pos;
  char c;
  int bytes_read;
  bytes_read = 0;
  /* Allocate Memory for the case we have none */
  if(!(*ret_alloc))
    {
    *ret_alloc = BYTES_TO_ALLOC;
    *ret = realloc(*ret, *ret_alloc);
    **ret = '\0';
    }
  pos = *ret;
  while(1)
    {
    if(!bgav_read_data_fd(opt, fd, (uint8_t*)(&c), 1, milliseconds))
      {
      if(!bytes_read)
        return 0;
      break;
      }
    /*
     *  Line break sequence
     *  is starting, remove the rest from the stream
     */
    if(c == '\n')
      {
      break;
      }
    /* Reallocate buffer */
    else if(c != '\r')
      {
      if(bytes_read+2 >= *ret_alloc)
        {
        *ret_alloc += BYTES_TO_ALLOC;
        *ret = realloc(*ret, *ret_alloc);
        pos = &((*ret)[bytes_read]);
        }
      /* Put the byte and advance pointer */
      *pos = c;
      pos++;
      bytes_read++;
      }
    }
  *pos = '\0';
  return 1;
  }

int bgav_read_data_fd(const bgav_options_t * opt,
                      int fd, uint8_t * ret, int len, int milliseconds)
  {
  int bytes_read = 0;
  int result;
  fd_set rset;
  struct timeval timeout;

  int flags = 0;


  //  if(milliseconds < 0)
  //    flags = MSG_WAITALL;
  
  while(bytes_read < len)
    {
    if(milliseconds >= 0)
      { 
      FD_ZERO (&rset);
      FD_SET  (fd, &rset);
     
      timeout.tv_sec  = milliseconds / 1000;
      timeout.tv_usec = (milliseconds % 1000) * 1000;
    
      if((result = select (fd+1, &rset, NULL, NULL, &timeout)) <= 0)
        {
//        fprintf(stderr, "Read timed out %d\n", milliseconds);
        return bytes_read;
        }
      }

    result = recv(fd, ret + bytes_read, len - bytes_read, flags);
    if(result > 0)
      bytes_read += result;
    else if(result <= 0)
      {
      if(result <0)
        bgav_log(opt, BGAV_LOG_ERROR, LOG_DOMAIN,
                 "recv failed: %s", strerror(errno));
      return bytes_read;
      }
    }
  return bytes_read;
  }

/* Break string into substrings separated by spaces */

char ** bgav_stringbreak(const char * str, char sep)
  {
  int len, i;
  int num;
  int index;
  char ** ret;
  len = strlen(str);
  if(!len)
    {
    num = 0;
    return NULL;
    }
  
  num = 1;
  for(i = 0; i < len; i++)
    {
    if(str[i] == sep)
      num++;
    }
  ret = calloc(num+1, sizeof(char*));

  index = 1;
  ret[0] = bgav_strdup(str);
  
  for(i = 0; i < len; i++)
    {
    if(ret[0][i] == sep)
      {
      if(index < num)
        ret[index] = ret[0] + i + 1;
      ret[0][i] = '\0';
      index++;
      }
    }
  return ret;
  }

void bgav_stringbreak_free(char ** str)
  {
  free(str[0]);
  free(str);
  }

int bgav_slurp_file(const char * location,
                    uint8_t ** ret,
                    int * ret_alloc,
                    int * size,
                    const bgav_options_t * opt)
  {
  bgav_input_context_t * input;
  input = bgav_input_create(opt);
  if(!bgav_input_open(input, location))
    {
    bgav_input_destroy(input);
    return 0;
    }
  if(!input->total_bytes)
    {
    bgav_input_destroy(input);
    return 0;
    }
  if(*ret_alloc < input->total_bytes)
    {
    *ret_alloc = input->total_bytes + 128;
    *ret = realloc(*ret, *ret_alloc);
    }
  
  if(bgav_input_read_data(input, *ret, input->total_bytes) <
     input->total_bytes)
    {
    free(ret);
    bgav_input_destroy(input);
    return 0;
    }
  *size = input->total_bytes;
  bgav_input_destroy(input);
  return 1;
  }

int bgav_check_file_read(const char * filename)
  {
  if(access(filename, R_OK))
    return 0;
  return 1;
  }

/* Search file for writing */

char * bgav_search_file_write(const bgav_options_t * opt,
                              const char * directory, const char * file)
  {
  char * home_dir;
  char * testpath;
  char * pos1;
  char * pos2;
  FILE * testfile;

  if(!file)
    return NULL;
  
  testpath = malloc((PATH_MAX+1) * sizeof(char));
  
  home_dir = getenv("HOME");

  /* Try to open the file */

  snprintf(testpath, PATH_MAX,
           "%s/.%s/%s/%s", home_dir, PACKAGE, directory, file);
  testfile = fopen(testpath, "a");
  if(testfile)
    {
    fclose(testfile);
    return testpath;
    }
  
  if(errno != ENOENT)
    {
    free(testpath);
    return NULL;
    }

  /*
   *  No such file or directory can mean, that the directory 
   *  doesn't exist
   */
  
  pos1 = &testpath[strlen(home_dir)+1];
  
  while(1)
    {
    pos2 = strchr(pos1, '/');

    if(!pos2)
      break;

    *pos2 = '\0';

#ifdef _WIN32
    if(mkdir(testpath) == -1)
#else
    if(mkdir(testpath, S_IRUSR|S_IWUSR|S_IXUSR) == -1)
#endif
      {
      if(errno != EEXIST)
        {
        *pos2 = '/';
        break;
        }
      }
    else
      bgav_log(opt, BGAV_LOG_INFO, LOG_DOMAIN,
               "Created directory %s", testpath);
    
    *pos2 = '/';
    pos1 = pos2;
    pos1++;
    }

  /* Try once more to open the file */

  testfile = fopen(testpath, "w");

  if(testfile)
    {
    fclose(testfile);
    return testpath;
    }
  free(testpath);
  return NULL;
  }

char * bgav_search_file_read(const bgav_options_t * opt,
                             const char * directory, const char * file)
  {
  char * home_dir;
  char * test_path;

  home_dir = getenv("HOME");

  test_path = malloc((PATH_MAX+1) * sizeof(*test_path));
  snprintf(test_path, PATH_MAX, "%s/.%s/%s/%s",
           home_dir, PACKAGE, directory, file);

  if(bgav_check_file_read(test_path))
    return test_path;

  free(test_path);
  return NULL;
  }

int bgav_match_regexp(const char * str, const char * regexp)
  {
  int ret = 0;
  regex_t exp;
  memset(&exp, 0, sizeof(exp));

  /*
    `REG_EXTENDED'
     Treat the pattern as an extended regular expression, rather than
     as a basic regular expression.

     `REG_ICASE'
     Ignore case when matching letters.

     `REG_NOSUB'
     Don't bother storing the contents of the MATCHES-PTR array.

     `REG_NEWLINE'
     Treat a newline in STRING as dividing STRING into multiple lines,
     so that `$' can match before the newline and `^' can match after.
     Also, don't permit `.' to match a newline, and don't permit
     `[^...]' to match a newline.
  */
  
  regcomp(&exp, regexp, REG_NOSUB|REG_EXTENDED);
  if(!regexec(&exp, str, 0, NULL, 0))
    ret = 1;
  regfree(&exp);
  return ret;
  }

