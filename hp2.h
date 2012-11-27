#ifndef HP2_H
#define HP2_H

#include <sys/types.h>


enum hp2_datum_type {
  HP2_NONE,
  HP2_MSG_START,
  HP2_METHOD, /* request method, E.G. "GET" */
  HP2_REASON, /* response reason, E.G. "Not Found" */
  HP2_URL, /* request url, E.G. "/favicon.ico" */
  HP2_FIELD,
  HP2_VALUE,
  HP2_HEADERS_COMPLETE,
  HP2_BODY, /* body may be broken up into many chunks */
  HP2_MSG_COMPLETE,
  HP2_ERROR
};

typedef struct {
  short state;
  /* These values should be copied out the struct on HP2_HEADERS_COMPLETE. */
  unsigned short version_major;
  unsigned short version_minor;
  short upgrade;
  ssize_t content_length; /* -1 means unknown body length */
  enum hp2_datum_type last_datum;
  unsigned int code; /* responses only. E.G. 200, 404. */
} hp2_parser;

typedef struct {
  enum hp2_datum_type type;

  /* If set to 1, close the connection. Do not call hp2_parse again. */
  short done;

  /* If partial is 1, the next datum will be of the same type. This can happen
   * in the case that a string is split over multiple buffers.
   * Used for HP2_METHOD, HP2_REASON, HP2_URL, HP2_FIELD,
   * HP2_VALUE, HP2_BODY
   */
  short partial;

  /* Used for HP2_METHOD, HP2_REASON, HP2_URL, HP2_FIELD,
   * HP2_VALUE, HP2_BODY. Otherwise NULL. */
  char* start; 

  /* Call hp2_parse() again starting here if done is 0. */
  char* end;
} hp2_datum;

/* Initializes an HTTP request parser. Used in HTTP servers. */
void hp2_req_init(hp2_parser* parser);


/* buf points to buffer filled with HTTP data. buflen is the length of the
 * buffer.
 *
 * The parser will parse until it has one datum and then return.
 *
 * The parser must be restarted on datum.end
 */
hp2_datum hp2_parse(hp2_parser* parser, char* buf, size_t buflen);



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
