#include <stddef.h>
#include <assert.h>
#include "hp2.h"


enum state {
  S_METHOD_START = 0,
  S_METHOD = 1,
  S_URL_START = 2,
  S_URL,
  S_REQ_H,
  S_REQ_HT,
  S_REQ_HTT,
  S_REQ_HTTP,
  S_REQ_VMAJOR,
  S_REQ_VPERIOD,
  S_REQ_VMINOR,
  S_REQ_CR,
  S_REQ_CRLF,
  S_FIELD_START,
  S_FIELD_START_CR,
  S_FIELD,
  S_VALUE_START,
  S_VALUE,
  S_BODY_START
};

void hp2_req_init(hp2_parser* parser) {
  parser->state = S_METHOD_START;
  parser->version_major = 0;
  parser->version_minor = 0;
  parser->upgrade = 0;
  parser->content_length = -1;
  parser->code = 0;
}


/* TODO replace with lookup tables. */
#define IS_LETTER(c) (('a' <= (c) && (c) <= 'z') || ('A' <= (c) && (c) <= 'Z'))
#define IS_WHITESPACE(c) ((c) == ' ' || (c) == '\n' || (c) == '\r')
#define IS_NUMBER(c) ('0' <= (c) && (c) <= '9')
#define IS_URL_CHAR(c) (IS_LETTER(c) || (c) == '/' || (c) == ':' || IS_NUMBER(c))
#define IS_FIELD_CHAR(c) (IS_LETTER(c) || IS_NUMBER(c) || (c) == '-')


hp2_datum hp2_parse(hp2_parser* parser, char* data, size_t len) {
  hp2_datum datum;
  char* p = data;
  char* end = data + len;

  datum.type = parser->last_datum;
  datum.start = parser->last_datum == HP2_NONE ? NULL : p;
  datum.end = NULL;
  datum.done = 0;
  datum.partial = 0;

  while (p < end) {
    char c = *p;
    switch (parser->state) {
      case S_METHOD_START: {
        if (IS_LETTER(c)) {
          datum.start = p;
          datum.type = HP2_METHOD;
          parser->state = S_METHOD;
        } else if (!IS_WHITESPACE(c)) {
          goto error;
        }
        break;
      }

      case S_METHOD: {
        if (c == ' ') {
          assert(datum.type == HP2_METHOD);
          datum.end = p;
          parser->state = S_URL_START;
          goto datum_complete;
        }

        if (!IS_LETTER(c)) {
          goto error;
        }
        break;
      }

      case S_URL_START: {
        if (c == ' ') {
          continue;
        }

        if (!IS_URL_CHAR(c)) {
          goto error;
        }

        datum.type = HP2_URL;
        datum.start = p;
        parser->state = S_URL;
        break;
      }

      case S_URL: {
        assert(datum.type == HP2_URL);

        if (c == ' ') {
          datum.end = p;
          parser->state = S_REQ_H;
          goto datum_complete;
        }

        if (!IS_URL_CHAR(c)) {
          goto error;
        }
        break;
      }

      case S_REQ_H: {
        assert(datum.type == HP2_NONE);
        if (c == ' ') {
          ;
        } else if (c == 'H') {
          parser->state = S_REQ_HT;
        } else {
          goto error;
        }
        break;
      }

      case S_REQ_HT: {
        assert(datum.type == HP2_NONE);
        if (c == 'T') {
          parser->state = S_REQ_HTT;
        } else {
          goto error;
        }
        break;
      }

      case S_REQ_HTT: {
        assert(datum.type == HP2_NONE);
        if (c == 'T') {
          parser->state = S_REQ_HTTP;
        } else {
          goto error;
        }
        break;
      }

      case S_REQ_HTTP: {
        assert(datum.type == HP2_NONE);
        if (c == '/') {
          parser->state = S_REQ_VMAJOR;
        } else {
          goto error;
        }
        break;
      }

      case S_REQ_VMAJOR: {
        assert(datum.type == HP2_NONE);
        if (!IS_NUMBER(c)) {
          goto error;
        }
        parser->version_major += c - '0';
        parser->state = S_REQ_VPERIOD;
        break;
      }

      case S_REQ_VPERIOD: {
        assert(datum.type == HP2_NONE);
        if (c != '.') goto error;
        parser->state = S_REQ_VMINOR;
        break;
      }

      case S_REQ_VMINOR: {
        assert(datum.type == HP2_NONE);
        if (!IS_NUMBER(c)) {
          goto error;
        }
        parser->version_minor += c - '0';
        parser->state = S_REQ_CR;
        break;
      }

      case S_REQ_CR: {
        assert(datum.type == HP2_NONE);
        if (c == '\r') {
          parser->state = S_REQ_CRLF;
        } else if (c == '\n') { 
          parser->state = S_FIELD_START;
        } else if (c == ' ') {
          ;
        } else {
          goto error;
        }
        break;
      }

      case S_REQ_CRLF: {
        assert(datum.type == HP2_NONE);
        if (c == '\n') {
          parser->state = S_FIELD_START;
        } else {
          goto error;
        }
        break;
      }

      case S_FIELD_START: {
        assert(datum.type == HP2_NONE);
        if (c == '\r') {
          parser->state = S_FIELD_START_CR;
        } else if (c == '\n') {
          datum.type = HP2_HEADERS_COMPLETE;
          datum.end = p + 1;
          assert(datum.start == NULL);
          parser->state = S_BODY_START;
          goto datum_complete;
        } else if (c == '\t') {
          /* Tabs indicate continued header value */
          assert(0 && "Implement me");
        } else if (c == ' ') {
          ;
        } else if (IS_FIELD_CHAR(c)) {
          datum.type = HP2_FIELD;
          datum.start = p;
          parser->state = S_FIELD;
        }
        break;
      }

      case S_FIELD_START_CR: {
        assert(datum.type == HP2_NONE);
        if (c == '\n') {
          datum.type = HP2_HEADERS_COMPLETE;
          datum.end = p + 1;
          assert(datum.start == NULL);
          parser->state = S_BODY_START;
          goto datum_complete;
        } else {
          goto error;
        }
        break;
      }

      case S_FIELD: {
        assert(datum.type == HP2_FIELD);
        if (c == ':') {
          datum.end = p;
          assert(datum.partial == 0);
          parser->state = S_VALUE_START;
          goto datum_complete;
        }

        if (!IS_FIELD_CHAR(c)) {
          goto error;
        }
        break;
      }



    }
  }

  datum.end = p;
  datum.partial = 1;
  parser->last_datum = datum.type;
  return datum;

datum_complete:
  assert(datum.partial == 0);
  assert(datum.end);
  parser->last_datum = HP2_NONE;
  return datum;

error:
  datum.type = HP2_ERROR;
  datum.done = 1;
  datum.end = p;
  return datum;
}
