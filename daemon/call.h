#ifndef __CALL_H__
#define __CALL_H__




#include <sys/types.h>
#include <glib.h>
#include <time.h>
#include <pcre.h>

#include "control_tcp.h"
#include "control_udp.h"
#include "obj.h"
#include "aux.h"
#include "bencode.h"
#include "str.h"

struct poller;
struct control_stream;



struct peer;
struct callstream;
struct call;
struct callmaster;
struct redis;




enum stream_address_format {
	SAF_TCP,
	SAF_UDP,
	SAF_NG,
};
enum stream_direction {
	DIR_UNKNOWN = 0,
	DIR_INTERNAL,
	DIR_EXTERNAL,
};
enum call_opmode {
	OP_OFFER = 0,
	OP_ANSWER = 1,
};

struct stats {
	u_int64_t			packets;
	u_int64_t			bytes;
	u_int64_t			errors;
};

struct stream {
	struct in6_addr		ip46;
	u_int16_t		port;
	int			num;
};
struct stream_input {
	struct stream		stream;
	enum stream_direction	direction[2];
};
struct streamrelay {
	int			fd;
	int			fd_family;
	struct stream		peer;
	struct stream		peer_advertised;
	u_int16_t		localport;
	unsigned char		idx;
	struct peer		*up;
	struct stats		stats;
	struct stats		kstats;
	time_t			last;
};
struct peer {
	struct streamrelay	rtps[2];
	str			tag;
	char			*codec;
	unsigned char		idx;
	struct callstream	*up;
	int			desired_family;
	int			kernelized:1;
	int			filled:1;
	int			confirmed:1;
};
struct callstream {
	struct obj		obj;
	mutex_t			lock;
	struct peer		peers[2];
	struct call		*call;
	int			num;
};

struct call {
	struct obj		obj;

	struct callmaster	*callmaster;

	mutex_t			chunk_lock;
	GStringChunk		*chunk;

	mutex_t			lock;
	GQueue			*callstreams;
	GHashTable		*branches;

	str			callid;
	char			redis_uuid[37];
	time_t			created;
	time_t			lookup_done;
};

struct callmaster_config {
	int			kernelfd;
	unsigned int		kernelid;
	u_int32_t		ipv4;
	u_int32_t		adv_ipv4;
	struct in6_addr		ipv6;
	struct in6_addr		adv_ipv6;
	int			port_min;
	int			port_max;
	unsigned int		timeout;
	unsigned int		silent_timeout;
	struct redis		*redis;
	char			*b2b_url;
	unsigned char		tos;
};

struct callmaster;



struct callmaster *callmaster_new(struct poller *);
void callmaster_config(struct callmaster *m, struct callmaster_config *c);


str *call_request_tcp(char **, struct callmaster *);
str *call_lookup_tcp(char **, struct callmaster *);
void call_delete_tcp(char **, struct callmaster *);
void calls_status_tcp(struct callmaster *, struct control_stream *);

str *call_update_udp(char **, struct callmaster *);
str *call_lookup_udp(char **, struct callmaster *);
str *call_delete_udp(char **, struct callmaster *);
str *call_query_udp(char **, struct callmaster *);

const char *call_offer_ng(bencode_item_t *, struct callmaster *, bencode_item_t *);
const char *call_answer_ng(bencode_item_t *, struct callmaster *, bencode_item_t *);
const char *call_delete_ng(bencode_item_t *, struct callmaster *, bencode_item_t *);


void calls_dump_redis(struct callmaster *);

struct call *call_get_or_create(const str *callid, const str *viabranch, struct callmaster *m);
struct callstream *callstream_new(struct call *ca, int num);
void callstream_init(struct callstream *s, int port1, int port2);
void kernelize(struct callstream *c);
int call_stream_address(GString *o, struct peer *p, enum stream_address_format);

static inline char *call_strdup(struct call *c, const char *s) {
	char *r;
	if (!s)
		return NULL;
	mutex_lock(&c->chunk_lock);
	r = g_string_chunk_insert(c->chunk, s);
	mutex_unlock(&c->chunk_lock);
	return r;
}
static inline str *call_str_cpy(struct call *c, str *out, const str *in) {
	if (!in) {
		*out = STR_NULL;
		return out;
	}
	*out = *in;
	if (!in->s)
		return out;
	mutex_lock(&c->chunk_lock);
	out->s = g_string_chunk_insert_len(c->chunk, in->s, in->len);
	mutex_unlock(&c->chunk_lock);
	return out;
}
static inline str *call_str_dup(struct call *c, const str *in) {
	str *out;
	mutex_lock(&c->chunk_lock);
	out = str_chunk_insert(c->chunk, in);
	mutex_unlock(&c->chunk_lock);
	return out;
}
static inline str *call_str_init_dup(struct call *c, char *s) {
	str t;
	str_init(&t, s);
	return call_str_dup(c, &t);
}




#endif
