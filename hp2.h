/* hp2 = HTTP Parser 2
 * No syscalls - pure computation. Does not depend on libc.
 * No allocations - you own all the memory.
 * No callbacks. Simplier interface.
 * Supports both request and response parsing.
 *
 *
 * HTTP Features Supported:
 * - Full HTTP/1.1.
 * - Keep-Alive. AKA pipelined messages.
 * - Transfer-Encoding: chunked
 * - Custom request method (http-parser had a fixed list of methods).
 * - Trailing header.
 * - Streaming bodies.
 *
 * Not supported:
 * - headers split over multiple lines (with leading tabs)
 * - The key/value pairs attached to body chunks in chunked encoding. (Don't
 *   worry, no software on earth uses this. I want to keep it that way.)
 */

#ifndef HP2_H
#define HP2_H

#include <sys/types.h>

/* Every HTTP stream will contain one or more requests which are broken up
   into datums.

     (HP2_MSG_START
      HP2_METHOD HP2_URL
      (HP2_FIELD HP2_VALUE)*
      HP2_HEADER_END
      HP2_BODY*
      (HP2_FIELD HP2_VALUE)*
      HP2_MSG_END)+

   Note that a trailing header may be present. Check parser->trailing_header to
   know if this will be expected.
 */

enum hp2_datum_type {
  HP2_EAGAIN, /* Needs more input; read the next packet. */
  HP2_EOF, /* Designates end of HTTP data. Do not call hp2_parse() again. */
  HP2_ERROR, /* Bad HTTP. Close the connection. */

  HP2_MSG_START,
  HP2_METHOD, /* Request only. Method, E.G. "GET" */
  HP2_REASON, /* Response only. Reason, E.G. "Not Found" */
  HP2_URL, /* Request only. The requested URL, E.G. "/favicon.ico" */
  HP2_FIELD, /* E.G. "Content-Type" */
  HP2_VALUE, /* E.G. "text/plain" */
  HP2_HEADER_END,
  HP2_BODY, /* Many HP2_BODY chunks may appear in a row. */
  HP2_MSG_END
};

typedef struct {
  enum hp2_datum_type type;

  /* start, end delimit the data - like a string. But not all datums are
   * strings; some are just points, like HP2_MSG_START, HP2_HEADER_END,
   * HP2_MSG_END, and HP2_ERROR. They will have start and end pointing to
   * the same location. The other datums delimit strings:
   * HP2_METHOD, HP2_REASON, HP2_URL, HP2_FIELD, HP2_VALUE, HP2_BODY.
   */
  const char* start;
  const char* end;

  /* If partial is non-zero, the next datum will be of the same type. This can
   * happen in the case that a string is split over multiple packets. Used for
   * HP2_METHOD, HP2_REASON, HP2_URL, HP2_FIELD, HP2_VALUE, HP2_BODY.
   */
  char partial;
} hp2_datum;

typedef struct {
  /* private */
  char flags;
  enum hp2_datum_type last_datum;
  unsigned char state;
  unsigned char header_state;
  unsigned char i;
  const char* match;
  size_t chunk_read;
  size_t chunk_len;

  /* read-only */
  /* These values should be copied out the struct on HP2_HEADER_END. */
  unsigned char version_major;
  unsigned char version_minor;
  unsigned char trailing_header; /* 1 means trailing header may follow body */
  size_t content_read;
  char upgrade; /* 1 means that HTTP ends. Do not call hp2_parse() again. */
  ssize_t content_length; /* -1 means unknown body length */
  unsigned int code; /* responses only. E.G. 200, 404. */
} hp2_parser;

/* Initializes an HTTP request parser. Used in HTTP servers. */
void hp2_req_init(hp2_parser* parser);

/* buf is a buffer filled with HTTP data. buflen is the length of the
 * buffer.
 *
 * The hp2_parse() will parse until it has one datum and then return it.
 * The parser must be restarted repeatedly with buf = datum.end. Even if
 * datum.end == buf + buflen, that is, zero length in the buffer remaining,
 * continue to call hp2_parse() until HP2_EAGAIN, HP2_ERROR, or EP2_EOF is
 * returned.
 *
 * If datum.type == HP2_EAGAIN, the parser has completed parsing buf and needs
 * new input. (Supplied by your next call to recv(2).)
 */
hp2_datum hp2_parse(hp2_parser* parser, const char* buf, size_t buflen);


/* If you are writing a web server, stop here. The rest is for writing http
 * clients; that is, parsing the responses from web servers.
 */


#if 0
/* TODO */
enum hp2_req_type {
  HP2_HEAD,
  HP2_OTHER
};

/* Initializes a HTTP response parser. Used in HTTP clients.
 * response parsers must call hp2_res_req() for each request
 * issued.
 */
void hp2_res_init(hp2_parser* parser);

/* Each time a request is issued, call this function to notify the parser of the
 * history of outstanding requests. This is necessary information for parsing
 * the responses. If a HTTP client issues a HEAD request, the response may
 * contain a content length header without having a body.
 */
void hp2_res_req(hp2_parser*,
                 enum hp2_req_type* reqs,
                 size_t reqslen);
#endif

#endif  /* HP2_H */
