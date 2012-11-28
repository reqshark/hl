#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "hp2.h"
#include "test_data.h"

void expect_eq(const char* expected, hp2_datum datum) {
  int len = datum.end - datum.start;
  int expected_len = strlen(expected);

  if (len != expected_len) {
    printf("bad strlen. expected = %d, got = %d\n", expected_len, len);
    abort();
  }

  if (strncmp(datum.start, expected, len) != 0) {
    printf("bad str. expected = \"%s\"\n", expected);
    abort();
  }
}


void manual_test() {
  const struct message* r = &requests[CURL_GET];

  hp2_parser parser;
  hp2_req_init(&parser);

  int raw_len =  strlen(r->raw);

  int len = raw_len;
  const char* buf = r->raw;

  /* Method = "GET" */ 
  hp2_datum d = hp2_parse(&parser, buf, len);
  assert(d.type == HP2_METHOD);
  assert(d.partial == 0);
  assert(d.last == 0);
  expect_eq(r->method, d);

  /* URL = "/test" */
  buf = d.end;
  len -= d.end - buf;
  d = hp2_parse(&parser, buf, len);
  assert(d.type == HP2_URL);
  assert(d.partial == 0);
  assert(d.last == 0);
  expect_eq(r->request_url, d);

  /* FIELD = "User-Agent" */
  buf = d.end;
  len -= d.end - buf;
  d = hp2_parse(&parser, buf, len);
  assert(d.type == HP2_FIELD);
  assert(d.partial == 0);
  assert(d.last == 0);
  expect_eq(r->headers[0][0], d);

  assert(parser.version_major == r->http_major);
  assert(parser.version_minor == r->http_minor);

  /* VALUE = "curl/7.18.0 (i486-pc-linux-gnu) libcurl/7.18.0 OpenSSL/0.9.8g zlib/1.2.3.3 libidn/1.1" */
  buf = d.end;
  len -= d.end - buf;
  d = hp2_parse(&parser, buf, len);
  assert(d.type == HP2_VALUE);
  assert(d.partial == 0);
  assert(d.last == 0);
  expect_eq(r->headers[0][1], d);

  /* FIELD = "Host" */
  buf = d.end;
  len -= d.end - buf;
  d = hp2_parse(&parser, buf, len);
  assert(d.type == HP2_FIELD);
  assert(d.partial == 0);
  assert(d.last == 0);
  expect_eq(r->headers[1][0], d);

  /* VALUE = "0.0.0.0=5000" */
  buf = d.end;
  len -= d.end - buf;
  d = hp2_parse(&parser, buf, len);
  assert(d.type == HP2_VALUE);
  assert(d.partial == 0);
  assert(d.last == 0);
  expect_eq(r->headers[1][1], d);

  /* FIELD = "Accept" */
  buf = d.end;
  len -= d.end - buf;
  d = hp2_parse(&parser, buf, len);
  assert(d.type == HP2_FIELD);
  assert(d.partial == 0);
  assert(d.last == 0);
  expect_eq(r->headers[2][0], d);

  // VALUE = "*/*"
  buf = d.end;
  len -= d.end - buf;
  d = hp2_parse(&parser, buf, len);
  assert(d.type == HP2_VALUE);
  assert(d.partial == 0);
  assert(d.last == 0);
  expect_eq(r->headers[2][1], d);

  // HEADERS_COMPLETE
  buf = d.end;
  len -= d.end - buf;
  d = hp2_parse(&parser, buf, len);
  assert(d.type == HP2_HEADERS_COMPLETE);
  assert(d.partial == 0);
  assert(d.last == 0);
  assert(d.start == NULL);

  // We're at the end of the buffer
  assert(d.end == r->raw + raw_len);

  // MSG_COMPLETE
  buf = d.end;
  len -= d.end - buf;
  d = hp2_parse(&parser, buf, len);
  assert(d.type == HP2_MSG_COMPLETE);
  assert(d.partial == 0);
  assert(d.start == NULL);

  // We're still at the end of the buffer
  assert(d.end == r->raw + raw_len);

  // If we call hp2_parse() again, we get HP2_EAGAIN.
  d = hp2_parse(&parser, d.end, 0);
  assert(d.type == HP2_EAGAIN);
}


int main() {
  printf("sizeof(hp2_datum) = %d\n", (int)sizeof(hp2_datum));
  printf("sizeof(hp2_parser) = %d\n", (int)sizeof(hp2_parser));
  manual_test();
  return 0;
}
