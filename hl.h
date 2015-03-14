/* hl = HTTP Lexer
 *
 * Features:
 * - HTTP/1.1.
 * - Keep-Alive. AKA pipelined messages.
 * - Transfer-Encoding: chunked
 * - Trailing header.
 * - Streaming bodies.
 * - No syscalls - pure computation.
 * - No allocations - you own all the memory.
 * - No callbacks.
 * - Supports both request and response parsing.
 * - writen in C89 standard for maximum portability.
 *
 * Not supported:
 * - headers split over multiple lines (with leading tabs)
 * - The key/value pairs attached to body chunks in chunked encoding. (Don't
 *   worry, no software on earth uses this. I want to keep it that way.)
 */

#ifndef HL_H
#define HL_H

#include <sys/types.h>

/* Every HTTP stream will contain one or more requests which are broken up
   into tokens.

   (HL_MSG_START
    HL_METHOD HL_URL
    (HL_FIELD HL_VALUE)*
    HL_HEADER_END
    HL_BODY*
    (HL_FIELD HL_VALUE)*
    HL_MSG_END)+
    HL_EOF

   Note that a trailing header may be present after the body. However, this is
   very uncommon. See RFC 2616 14.40.
 */

typedef enum {
  HL_EAGAIN, /* Needs more input; read the next packet. */
  HL_EOF, /* Designates end of HTTP data. Do not call hl_execute() again. */
  HL_ERROR, /* Bad HTTP. Close the connection. */

  HL_MSG_START,
  HL_METHOD, /* Request only. Method, E.G. "GET" */
  HL_REASON, /* Response only. Reason, E.G. "Not Found" */
  HL_URL, /* Request only. The requested URL, E.G. "/favicon.ico" */
  HL_FIELD, /* E.G. "Content-Type" */
  HL_VALUE, /* E.G. "text/plain" */
  HL_HEADER_END,
  HL_BODY, /* Many HL_BODY chunks may appear in a row. */
  HL_MSG_END
} hl_token_kind;

typedef struct {
  hl_token_kind kind;

  /* start, end delimit the data - like a string. But not all tokens are
   * strings; some are just points, like HL_MSG_START, HL_HEADER_END,
   * HL_MSG_END, and HL_ERROR. They will have start and end pointing to
   * the same location. The other tokens delimit strings:
   * HL_METHOD, HL_REASON, HL_URL, HL_FIELD, HL_VALUE, HL_BODY.
   */
  const char* start;
  const char* end;

  /* If partial is non-zero, the next token will be of the same kind. This can
   * happen in the case that a string is split over multiple packets. Used for
   * HL_METHOD, HL_REASON, HL_URL, HL_FIELD, HL_VALUE, HL_BODY.
   */
  char partial;
} hl_token;

typedef struct {
  /* private */
  char flags;
  hl_token_kind last;
  unsigned char state;
  unsigned char header_state;
  unsigned char i;
  const char* match;
  size_t chunk_read;
  size_t chunk_len;

  /* read-only */
  /* These values should be copied out the struct on HL_HEADER_END. */
  unsigned char version_major;
  unsigned char version_minor;
  size_t content_read;
  char upgrade; /* 1 means that HTTP ends. Do not call hl_execute() again. */
  ssize_t content_length; /* -1 means unknown body length */
  unsigned int code; /* responses only. E.G. 200, 404. */
} hl_lexer;

/* Initializes an HTTP request lexer. Used in HTTP servers. */
void hl_req_init(hl_lexer* lexer);

/* buf is a buffer filled with HTTP data. buflen is the length of the
 * buffer.
 *
 * The hl_execute() will parse until it has one token and then return it.
 * Yes, it's copy-by-value. sizeof(hl_token) == 32 - not a big deal.
 *
 * hl_execute() must be restarted repeatedly with buf = token.end. Even if
 * token.end == buf + buflen, that is, zero length in the buffer remaining,
 *
 * Continue to call hl_execute() until HL_EAGAIN, EP2_EOF, or HL_ERROR is
 * returned.
 *
 * If token.kind == HL_EAGAIN, the lexer has completed parsing buf and needs
 * new input. (Supplied by your next call to recv(2).)
 *
 * See tests.c for example usage.
 */
hl_token hl_execute(hl_lexer* lexer, const char* buf, size_t buflen);


/* If you are writing a web server, stop here. The rest is for writing http
 * clients; that is, parsing the responses from web servers.
 */


#if 0
/* TODO */
enum hl_req_type {
  HL_HEAD,
  HL_OTHER
};

/* Initializes a HTTP response lexer. Used in HTTP clients.
 * response lexers must call hl_res_req() for each request
 * issued.
 */
void hl_res_init(hl_lexer* lexer);

/* Each time a request is issued, call this function to notify the lexer of the
 * history of outstanding requests. This is necessary information for parsing
 * the responses. If a HTTP client issues a HEAD request, the response may
 * contain a content length header without having a body.
 */
void hl_res_req(hl_lexer*,
                enum hl_req_type* reqs,
                size_t reqslen);
#endif

#endif  /* HL_H */
