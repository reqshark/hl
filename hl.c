#include <stddef.h>
#include <assert.h>
#include "hl.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))

enum flag {
  F_CONNECTION_KEEP_ALIVE     = 0x01,
  F_CONNECTION_CLOSE          = 0x02,
  F_TRANSFER_ENCODING_CHUNKED = 0x04,
  F_TRAILER                   = 0x08,
  F_UPGRADE                   = 0x10
};

enum state {
  S_REQ_START,
  S_MSG_END,
  S_EOF,
  S_UPGRADE,

  S_METHOD_START,
  S_METHOD,
  S_URL_START,
  S_URL,
  S_REQ_H,
  S_REQ_HT,
  S_REQ_HTT,
  S_REQ_HTTP,
  S_REQ_HTTP_SLASH,
  S_REQ_VMAJOR,
  S_REQ_VPERIOD,
  S_REQ_VMINOR,
  S_REQ_CR,
  S_REQ_CRLF,
  S_FIELD_START,
  S_FIELD_START_CR,
  S_FIELD,
  S_FIELD_COLON,
  S_VALUE_START,
  S_VALUE,
  S_VALUE_CR,
  S_VALUE_CRLF,
  S_IDENTITY_CONTENT,

  S_CHUNK_START,
  S_CHUNK_LEN,
  S_CHUNK_LEN_CRLF,
  S_CHUNK_KV,
  S_CHUNK_CONTENT,
  S_CHUNK_CONTENT_CR,
  S_CHUNK_CONTENT_CRLF,

  HS_ANYTHING,
  HS_C,
  HS_CO,
  HS_CON,
  HS_MATCH_CONNECTION,
  HS_MATCH_CONTENT_LENGTH,
  HS_MATCH_TRANSFER_ENCODING,
  HS_MATCH_UPGRADE,
  HS_MATCH_KEEP_ALIVE,
  HS_MATCH_CLOSE
};

void hl_req_init(hl_lexer* lexer) {
  lexer->last = HL_EAGAIN;
  lexer->state = S_REQ_START;
}


#define LOWER(c) ((c) | 0x20)
/* TODO replace with lookup tables. */
#define IS_LETTER(c) (('a' <= (c) && (c) <= 'z') || ('A' <= (c) && (c) <= 'Z'))
#define IS_WHITESPACE(c) ((c) == ' ' || (c) == '\n' || (c) == '\r')
#define IS_NUMBER(c) ('0' <= (c) && (c) <= '9')
#define IS_URL_CHAR(c) is_url_char[(unsigned char)c]
#define IS_METHOD_CHAR(c) (IS_LETTER(c) || c == '-')
#define IS_FIELD_CHAR(c) (IS_LETTER(c) || IS_NUMBER(c) || (c) == '-')
#define IS_CHUNK_KV_CHAR(c) (IS_LETTER(c) || IS_NUMBER(c) || (c) == '=' || \
                             (c) == ' ' || (c) == ';')
#define UNHEX(c) ((int)unhex[(unsigned char)c])


const char is_url_char[] = {
  /*  NUL SOH STX ETX EOT ENQ ACK \a \b \t \n \v \f \r SO SI  */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  /*  DLE DC1 DC2 DC3 DC4 NAK SYN ETB CAN EM SUB ESC FS GS RS US  */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  /*  SPACE ! " # $ % & ´ ( ) * + , - . /  */
  0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1,
  /*  0 1 2 3 4 5 6 7 8 9 : ; < = > ?  */
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  /*  @ A B C D E F G H I J K L M N O  */
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  /*  P Q R S T U V W X Y Z [ \ ] ^ _  */
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  /*  ` a b c d e f g h i j k l m n o  */
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  /*  p q r s t u v w x y z { | } ~ DEL */
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 0,
  /* If the 7th bit is set, then it's part of a UTF8 char. Accepted. */
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
};

const char unhex[] = {
  /*  NUL SOH STX ETX EOT ENQ ACK \a \b \t \n \v \f \r SO SI  */
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  /*  DLE DC1 DC2 DC3 DC4 NAK SYN ETB CAN EM SUB ESC FS GS RS US  */
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  /*  SPACE ! " # $ % & ´ ( ) * + , - . /  */
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  /*  0 1 2 3 4 5 6 7 8 9 : ; < = > ?  */
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, -1, -1, -1, -1, -1, -1,
  /*  @ A B C D E F G H I J K L M N O  */
  -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  /*  P Q R S T U V W X Y Z [ \ ] ^ _  */
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  /*  ` a b c d e f g h i j k l m n o  */
  -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  /*  p q r s t u v w x y z { | } ~ DEL */
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  /* 7bit set */
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

#define CONNECTION "connection"
#define CONTENT_LENGTH "content-length"
#define TRANSFER_ENCODING "transfer-encoding"
#define UPGRADE "upgrade"
#define CHUNKED "chunked"
#define KEEP_ALIVE "keep-alive"
#define CLOSE "close"
#define UPGRADE "upgrade"


/* Don't call this directly, Use HEADER_COMPLETE macro.  This is code to be run
 * when the normal header is complete. The HEADER_COMPLETE macro handles both
 * normal and trialing header.
 */
static void basic_header_complete(hl_lexer* lexer,
                                  const char* head,
                                  hl_token* token) {
  assert(token->start == NULL);
  assert(!(lexer->flags & F_TRAILER));

  if (lexer->flags & F_UPGRADE) {
    lexer->state = S_MSG_END;
  } else if (lexer->flags & F_TRANSFER_ENCODING_CHUNKED) {
    lexer->state = S_CHUNK_START;
  } else {
    if (lexer->content_length <= 0) {
      /* XXX should check connection header to see if we're accepting more */
      lexer->state = S_MSG_END;
    } else {
      lexer->state = S_IDENTITY_CONTENT;
    }
  }

  token->kind = HL_HEADER_END;
  token->partial = 0;
  token->start = token->end = head + 1;

  lexer->content_read = 0;
}

#define HEADER_COMPLETE()                           \
  do {                                              \
    if (lexer->flags & F_TRAILER) {                \
      lexer->state = S_MSG_END;                    \
    } else {                                        \
      basic_header_complete(lexer, head, &token);  \
      goto token_complete;                          \
    }                                               \
  } while(0)

int should_keep_alive(hl_lexer* lexer) {
  if (lexer->version_major == 1 && lexer->version_minor == 1) {
    if (lexer->flags & F_CONNECTION_CLOSE) {
      return 0;
    } else {
      return 1;
    }
  } else {
    if (lexer->flags & F_CONNECTION_KEEP_ALIVE) {
      return 1;
    } else {
      return 0;
    }
  }
}


hl_token hl_execute(hl_lexer* lexer, const char* data, size_t len) {
  hl_token token; /* returned token */
  const char* head = data; /* lexer head */
  const char* end = data + len;
  int to_read;
  int value;

  token.kind = lexer->last;
  token.start = lexer->last == HL_EAGAIN ? NULL : data;
  token.end = NULL;
  token.partial = 0;

  for (; head < end ||
       lexer->state == S_MSG_END ||
       lexer->state == S_EOF ||
       lexer->state == S_UPGRADE;
       head++) {
    char c = *head;
    switch (lexer->state) {
      default: assert(0);

      case S_REQ_START: {
        if (IS_METHOD_CHAR(c)) {
          /* Reset the lexer */
          lexer->flags = 0;
          lexer->content_length = -1; /* Indicates no content-length header. */
          lexer->version_major = 0;
          lexer->version_minor = 9;
          lexer->upgrade = 0;
          lexer->content_read = 0;

          token.start = token.end = head;
          token.kind = HL_MSG_START;
          lexer->state = S_METHOD_START;
          goto token_complete;
        } else if (!IS_WHITESPACE(c)) {
          goto error;
        }
        break;
      }

      case S_MSG_END: {
        token.kind = HL_MSG_END;
        token.start = token.end = head;

        if (lexer->flags & F_UPGRADE) {
          lexer->state = S_EOF;
        } else {
          /* Check to see if we have persistant connection. */
          lexer->state = should_keep_alive(lexer) ? S_REQ_START : S_EOF;
        }

        goto token_complete;
        break;
      }

      case S_UPGRADE:
      case S_EOF: {
        token.kind = HL_EOF;
        token.start = token.end = head;
        goto token_complete;
      }

      case S_METHOD_START: {
        /* We already checked this in S_REQ_START */
        assert(IS_METHOD_CHAR(c));

        token.start = head;
        token.kind = HL_METHOD;
        lexer->state = S_METHOD;
        break;
      }

      case S_METHOD: {
        if (c == ' ') {
          assert(token.kind == HL_METHOD);
          token.end = head;
          lexer->state = S_URL_START;
          goto token_complete;
        }

        if (!IS_METHOD_CHAR(c)) {
          goto error;
        }
        break;
      }

      case S_URL_START: {
        if (c != ' ') {
          if (!IS_URL_CHAR(c)) {
            goto error;
          }

          token.kind = HL_URL;
          token.start = head;
          lexer->state = S_URL;
        }
        break;
      }

      case S_URL: {
        assert(token.kind == HL_URL);

        if (!IS_URL_CHAR(c)) {
          token.end = head;
          lexer->state = S_REQ_H;
          goto token_complete;
        }
        break;
      }

      case S_REQ_H: {
        assert(token.kind == HL_EAGAIN);
        if (c == ' ') {
          ;
        } else if (c == 'H') {
          lexer->state = S_REQ_HT;
        } else if (c == '\r') {
          lexer->state = S_REQ_CRLF;
        } else if (c == '\n') {
          lexer->state = S_FIELD_START;
        } else {
          goto error;
        }
        break;
      }

      case S_REQ_HT: {
        assert(token.kind == HL_EAGAIN);
        if (c == 'T') {
          lexer->state = S_REQ_HTT;
        } else {
          goto error;
        }
        break;
      }

      case S_REQ_HTT: {
        assert(token.kind == HL_EAGAIN);
        if (c == 'T') {
          lexer->state = S_REQ_HTTP;
        } else {
          goto error;
        }
        break;
      }

      case S_REQ_HTTP: {
        assert(token.kind == HL_EAGAIN);
        if (c == 'P') {
          lexer->state = S_REQ_HTTP_SLASH;
        } else {
          goto error;
        }
        break;
      }

      case S_REQ_HTTP_SLASH: {
        if (c == '/') {
          lexer->version_major = 0;
          lexer->version_minor = 0;
          lexer->state = S_REQ_VMAJOR;
        } else {
          goto error;
        }
        break;
      }

      case S_REQ_VMAJOR: {
        assert(token.kind == HL_EAGAIN);
        if (!IS_NUMBER(c)) {
          goto error;
        }
        lexer->version_major += c - '0';
        lexer->state = S_REQ_VPERIOD;
        break;
      }

      case S_REQ_VPERIOD: {
        assert(token.kind == HL_EAGAIN);
        if (c != '.') goto error;
        lexer->state = S_REQ_VMINOR;
        break;
      }

      case S_REQ_VMINOR: {
        assert(token.kind == HL_EAGAIN);
        if (!IS_NUMBER(c)) {
          goto error;
        }
        lexer->version_minor += c - '0';
        lexer->state = S_REQ_CR;
        break;
      }

      case S_REQ_CR: {
        assert(token.kind == HL_EAGAIN);
        if (c == '\r') {
          lexer->state = S_REQ_CRLF;
        } else if (c == '\n') {
          lexer->state = S_FIELD_START;
        } else if (c == ' ') {
          ;
        } else {
          goto error;
        }
        break;
      }

      case S_REQ_CRLF: {
        assert(token.kind == HL_EAGAIN);
        if (c == '\n') {
          lexer->state = S_FIELD_START;
        } else {
          goto error;
        }
        break;
      }

      case S_FIELD_START: {
        assert(token.kind == HL_EAGAIN);

        if (c == '\r') {
          lexer->state = S_FIELD_START_CR;
        } else if (c == '\n') {
          HEADER_COMPLETE();
        } else if (c == '\t') {
          /* Tabs indicate continued header value */
          assert(0 && "Implement me");
          goto error;
        } else if (c == ' ') {
          ;
        } else if (IS_FIELD_CHAR(c)) {
          token.kind = HL_FIELD;
          token.start = head;
          if (lexer->flags & F_TRAILER) {
            /* Trailing field/values don't need to be parsed. */
            lexer->header_state = HS_ANYTHING;
          } else {
            /* We need to read a couple of the headers. In particular
             * Content-Length, Connection, and Transfer-Encoding.
             */
            c = LOWER(c);
            switch (c) {
              case 'c':
                lexer->header_state = HS_C;
                break;

              case 'u':
                lexer->header_state = HS_MATCH_UPGRADE;
                lexer->match = UPGRADE;
                lexer->i = 1;
                break;

              case 't':
                lexer->header_state = HS_MATCH_TRANSFER_ENCODING;
                lexer->match = TRANSFER_ENCODING;
                lexer->i = 1;
                break;

              default:
                lexer->header_state = HS_ANYTHING;
                break;
            }
          }
          lexer->state = S_FIELD;
        }
        break;
      }

      case S_FIELD_START_CR: {
        assert(token.kind == HL_EAGAIN);
        if (c == '\n') {
          HEADER_COMPLETE();
        } else {
          goto error;
        }
        break;
      }

      case S_FIELD: {
        assert(token.kind == HL_FIELD);

        if (c == ':') {
          if (lexer->header_state != HS_ANYTHING &&
              lexer->match[lexer->i] != '\0') {
            lexer->header_state = HS_ANYTHING;
          }

          token.end = head;
          assert(token.partial == 0);
          lexer->state = S_FIELD_COLON;
          head--; /* XXX back up head... */
          goto token_complete;
        }

        if (lexer->header_state != HS_ANYTHING) {
          c = LOWER(c);
          switch (lexer->header_state) {
            case HS_C: {
              lexer->header_state = c == 'o' ? HS_CO : HS_ANYTHING;
              break;
            }

            case HS_CO: {
              lexer->header_state = c == 'n' ? HS_CON : HS_ANYTHING;
              break;
            }

            case HS_CON: {
              if (c == 't') {
                lexer->header_state = HS_MATCH_CONTENT_LENGTH;
                lexer->match = CONTENT_LENGTH;
                lexer->i = 4;
                break;
              } else if (c == 'n') {
                lexer->header_state = HS_MATCH_CONNECTION;
                lexer->match = CONNECTION;
                lexer->i = 4;
                break;
              } else {
                lexer->header_state = HS_ANYTHING;
              }
              break;
            }

            case HS_MATCH_CONTENT_LENGTH:
            case HS_MATCH_CONNECTION:
            case HS_MATCH_TRANSFER_ENCODING:
            case HS_MATCH_UPGRADE:
              if (lexer->match[lexer->i++] == c) break;
              lexer->header_state = HS_ANYTHING;
              break;

            default:
              assert(0);
              goto error;
          }
        }

        if (!IS_FIELD_CHAR(c)) {
          goto error;
        }
        break;
      }

      case S_FIELD_COLON: {
        assert(token.kind == HL_EAGAIN);
        if (c != ':') goto error;
        lexer->state = S_VALUE_START;
        break;
      }

      case S_VALUE_START:
        /* Skip spaces at the beginning of values */
        if (c == ' ') break;
        assert(token.kind == HL_EAGAIN);
        token.kind = HL_VALUE;
        token.start = head;
        lexer->state = S_VALUE;

        switch (lexer->header_state) {
          case HS_ANYTHING:
            break;

          case HS_MATCH_CONTENT_LENGTH: {
            lexer->content_length = 0;
            break;
          }

          case HS_MATCH_CONNECTION: {
            c = LOWER(c);
            if (c == 'k') {
              lexer->header_state = HS_MATCH_KEEP_ALIVE;
              lexer->match = KEEP_ALIVE;
              lexer->i = 0;
            } else if (c == 'c') {
              lexer->header_state = HS_MATCH_CLOSE;
              lexer->match = CLOSE;
              lexer->i = 0;
            } else {
              lexer->header_state = HS_ANYTHING;
            }
            break;
          }

          case HS_MATCH_UPGRADE: {
            lexer->header_state = HS_ANYTHING;
            lexer->flags |= F_UPGRADE;
            break;
          }

          case HS_MATCH_TRANSFER_ENCODING: {
            lexer->match = CHUNKED;
            lexer->i = 0;
            break;
          }

          default:
            assert(0 && "should not reach here");
        }
        /* pass-through to S_VALUE */

      case S_VALUE: {
        /* End of header value */
        if (c == '\r' || c == '\n') {
          assert(token.kind == HL_VALUE);
          token.end = head;
          lexer->state = c == '\r' ? S_VALUE_CR : S_VALUE_CRLF;

          if (lexer->header_state != HS_ANYTHING) {
            switch (lexer->header_state) {
              case HS_MATCH_KEEP_ALIVE: {
                if (lexer->match[lexer->i] == '\0') {
                  lexer->flags |= F_CONNECTION_KEEP_ALIVE;
                }
                break;
              }

              case HS_MATCH_CLOSE: {
                if (lexer->match[lexer->i] == '\0') {
                  lexer->flags |= F_CONNECTION_CLOSE;
                }
                break;
              }

              case HS_MATCH_TRANSFER_ENCODING: {
                if (lexer->match[lexer->i] == '\0') {
                  lexer->flags |= F_TRANSFER_ENCODING_CHUNKED;
                }
                break;
              }
            }
          }

          head--; /* XXX back up head */
          goto token_complete;
        }

        /* header value */
        if (lexer->header_state != HS_ANYTHING) {
          c = LOWER(c);
          switch (lexer->header_state) {
            case HS_MATCH_CONTENT_LENGTH: {
              if (!IS_NUMBER(c)) goto error;
              lexer->content_length *= 10;
              lexer->content_length += c - '0';
              break;
            }

            case HS_MATCH_KEEP_ALIVE:
            case HS_MATCH_CLOSE:
            case HS_MATCH_TRANSFER_ENCODING: {
              if (lexer->match[lexer->i++] != c) {
                lexer->header_state = HS_ANYTHING;
              }
              break;
            }

            default:
              assert(0 && "should not reach here");
          }
        }

        break;
      }

      case S_VALUE_CR: {
        assert(token.kind == HL_EAGAIN);
        if (c != '\r') goto error;
        lexer->state = S_VALUE_CRLF;
        break;
      }

      case S_VALUE_CRLF: {
        assert(token.kind == HL_EAGAIN);
        if (c != '\n') goto error;
        lexer->state = S_FIELD_START;
        break;
      }

      case S_IDENTITY_CONTENT: {
        token.kind = HL_BODY;
        token.start = head;
        to_read = MIN(end - head,
                      lexer->content_length - lexer->content_read);
        lexer->content_read += to_read;
        head += to_read;

        if (lexer->content_length == lexer->content_read) {
          assert(token.kind == HL_BODY);
          token.end = head;
          lexer->state = S_MSG_END;
          goto token_complete;
        }
        break;
      }

      case S_CHUNK_START: {
        value = UNHEX(c);
        if (value < 0) goto error;
        lexer->chunk_read = 0;
        lexer->chunk_len = value;
        lexer->state = S_CHUNK_LEN;
        break;
      }

      case S_CHUNK_LEN: {
        value = UNHEX(c);
        if (value < 0) {
          if (c == '\r') {
            lexer->state = S_CHUNK_LEN_CRLF;
          } else if (IS_CHUNK_KV_CHAR(c)) {
            lexer->state = S_CHUNK_KV;
          } else {
            goto error;
          }
        } else {
          lexer->chunk_len *= 16;
          lexer->chunk_len += value;
        }
        break;
      }

      case S_CHUNK_KV: {
        /* We completely ignore chunked key-value pairs */
        if (IS_CHUNK_KV_CHAR(c)) {
          ;
        } else if (c == '\r') {
          lexer->state = S_CHUNK_LEN_CRLF;
        } else {
          goto error;
        }
        break;
      }

      case S_CHUNK_LEN_CRLF: {
        if (c != '\n') goto error;
        assert(lexer->chunk_read == 0);

        if (lexer->chunk_len == 0) {
          /* Last chunk. There may be trailing headers. RFC 2616 14.40. */
          lexer->state = S_FIELD_START;
          lexer->flags |= F_TRAILER;
        } else {
          lexer->state = S_CHUNK_CONTENT;
        }
        break;
      }

      case S_CHUNK_CONTENT: {
        assert(lexer->chunk_len > 0);
        token.kind = HL_BODY;
        token.start = head;
        to_read = MIN(end - head,
                      lexer->chunk_len - lexer->chunk_read);
        lexer->chunk_read += to_read;
        head += to_read;

        if (lexer->chunk_len == lexer->chunk_read) {
          assert(token.kind == HL_BODY);
          token.end = head;
          lexer->state = S_CHUNK_CONTENT_CR;
          goto token_complete;
        }
        break;
      }

      case S_CHUNK_CONTENT_CR: {
        if (c != '\r') goto error;
        lexer->state = S_CHUNK_CONTENT_CRLF;
        /* We shouldn't get here in the case that we're on the last chunk. */
        assert(lexer->chunk_len > 0);
        break;
      }

      case S_CHUNK_CONTENT_CRLF: {
        if (c != '\n') goto error;

        /* We shouldn't get here in the case that we're on the last chunk. */
        assert(lexer->chunk_len > 0);

        lexer->state = S_CHUNK_START;
        break;
      }
    }
  }

  token.end = head;
  token.partial = 1;
  lexer->last = token.kind;
  return token;

token_complete:
  assert(token.partial == 0);
  assert(token.end);
  lexer->last = HL_EAGAIN;
  return token;

error:
  token.kind = HL_ERROR;
  token.start = NULL;
  token.end = head;
  return token;
}
