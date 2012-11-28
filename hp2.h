/* hp2 = HTTP Parser 2
 * No syscalls, no allocations, pure computation, No callbacks.
 *
 * HTTP Features Supported:
 * - Full HTTP/1.1.
 * - Keep-Alive. AKA pipelined messages.
 * - Transfer-Encoding: chunked
 * - Custom request method (http-parser had a fixed list of methods).
 * - Trailing headers.
 * - Streaming bodies.
 *
 * Not supported:
 * - The key/value pairs attached to body chunks in chunked encoding. (Don't
 *   worry, no software on earth uses this. I want to keep it that way.)
 *
 * */
#ifndef HP2_H
#define HP2_H

#include <sys/types.h>

enum hp2_datum_type {
  HP2_EAGAIN, /* Needs more input; the next packet. */
  HP2_MSG_START,
  HP2_METHOD, /* Request only. Method, E.G. "GET" */
  HP2_REASON, /* Response only. Reason, E.G. "Not Found" */
  HP2_URL, /* Request URL, E.G. "/favicon.ico" */
  HP2_FIELD,
  HP2_VALUE,
  HP2_HEADERS_COMPLETE,
  HP2_BODY, /* Many HP2_BODY chunks may appear in a row */
  HP2_MSG_COMPLETE,
  HP2_ERROR /* Bad HTTP. Close the connection. */
};

typedef struct {
  enum hp2_datum_type type;

  /* If set to 1, close the connection. Do not call hp2_parse again. */
  short last;

  /* If partial is 1, the next datum will be of the same type. This can happen
   * in the case that a string is split over multiple packets.
   * Used for HP2_METHOD, HP2_REASON, HP2_URL, HP2_FIELD,
   * HP2_VALUE, HP2_BODY
   */
  short partial;

  /* Used for HP2_METHOD, HP2_REASON, HP2_URL, HP2_FIELD,
   * HP2_VALUE, HP2_BODY. Otherwise NULL.
   */
  const char* start; 

  /* Call hp2_parse() again starting here if last is 0. */
  const char* end;
} hp2_datum;

typedef struct {
  /* private */
  enum hp2_datum_type last_datum;
  short state;
  short header_state;
  short i;
  const char* match;

  /* read-only */
  /* These values should be copied out the struct on HP2_HEADERS_COMPLETE. */
  unsigned short version_major;
  unsigned short version_minor;
  short upgrade; /* 1 means that HTTP ends. Do not call hp2_parse() again. */
  ssize_t content_length; /* -1 means unknown body length */
  unsigned int code; /* responses only. E.G. 200, 404. */
} hp2_parser;

/* Initializes an HTTP request parser. Used in HTTP servers. */
void hp2_req_init(hp2_parser* parser);

/* buf is a buffer filled with HTTP data. buflen is the length of the
 * buffer.
 *
 * The hp2_parse() will parse until it has one datum and then return it.
 *
 * The parser must be restarted on datum.end.
 * 
 * If datum.last is 1, then you shouldn't continue to call hp2_parse().  You
 * should stop reading from the TCP socket when datum.last is 1. 
 *
 * If datum.type == HP2_EAGAIN, the parser has completed parsing buf
 * and needs new input. (Supplied by your next call to recv(2).)
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
