#include "control_ng_flags_parser.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ctype.h>

#include "log.h"
#include "log_funcs.h"


/**
 * Data structures.
 */

static const char *transports[] = {
		[0x00] = "RTP/AVP",
		[0x01] = "RTP/SAVP",
		[0x02] = "RTP/AVPF",
		[0x03] = "RTP/SAVPF",
		[0x04] = "UDP/TLS/RTP/SAVP",
		[0x06] = "UDP/TLS/RTP/SAVPF",
};

/**
 * Helpers.
 */

/* parsing of key and val from string */
static bool get_key_val(str * key, str * val, str *in_out)
{
	if (!str_token_sep(key, in_out, ' '))
		return false;
	// key=value ?
	str k;
	*val = STR_NULL;
	if (!str_token_sep(&k, key, '='))
		return true;
	*val = *key;
	*key = k;
	return true;
}

/* handle either "foo-bar" or "foo=bar" from flags */
static bool str_key_val_prefix(const str * p, const char * q,
		const str * v, str * out)
{
	if(str_eq(p, q)) {
		if(!v->s || !v->len)
			return 0;

		*out = *v;
		return true;
	}
	*out = *p;
	if (str_shift_cmp(out, q))
		return false;
	if(out->len < 2)
		return false;
	if(*out->s != '-')
		return false;
	out->s++;
	out->len--;
	return true;
}

static bool dummy_is_list(parser_arg a) {
	return false;
}
static str *dummy_get_str(parser_arg a, str *b) {
	*b = *a.str;
	return b;
}

const ng_parser_t dummy_parser = {
	.is_list = dummy_is_list,
	.get_str = dummy_get_str,
};

static bool parse_codec_to_dict(str * key, str * val, const char *cmp1, const char *cmp2,
		const char * dictstr, sdp_ng_flags *flags)
{
	str s;

	if(!str_key_val_prefix(key, cmp1, val, &s)) {
		if(!cmp2)
			return false;
		if(!str_key_val_prefix(key, cmp2, val, &s))
			return false;
	}

	call_ng_codec_flags(&dummy_parser, &STR(dictstr), &s, flags);

	return true;
}

/* parse codec related flags */
static bool parse_codecs(sdp_ng_flags *flags, str * key, str * val) {
	if (parse_codec_to_dict(key, val, "transcode",
				"codec-transcode", "transcode", flags) ||
		parse_codec_to_dict(key, val, "codec-strip",
				NULL, "strip", flags) ||
		parse_codec_to_dict(key, val, "codec-offer",
				NULL, "offer", flags) ||
		parse_codec_to_dict(key, val, "codec-mask",
				NULL, "mask", flags) ||
		parse_codec_to_dict(key, val, "codec-set",
				NULL, "set", flags) ||
		parse_codec_to_dict(key, val, "codec-accept",
				NULL, "accept", flags) ||
		parse_codec_to_dict(key, val, "codec-except",
				NULL, "except", flags))
	{
		return true;
	}

	return false;
}

/* prase transport, such as for example RTP/AVP */
static void parse_transports(unsigned int transport, sdp_ng_flags *out)
{
	const char * val = transports[transport & 0x007];
	if (!val)
		return;
	call_ng_main_flags(&dummy_parser, &STR_CONST("transport-protocol"), &STR(val), out);
}


static void rtpp_direction_flag(sdp_ng_flags *flags, unsigned int *flagnum, str *val) {
	static const str keys[2] = {STR_CONST("from-interface"), STR_CONST("to-interface")};
	if (*flagnum >= G_N_ELEMENTS(keys)) {
		ilog(LOG_WARN, "Too many 'direction=...' flags encountered");
		return;
	}
	str key = keys[(*flagnum)++];
	call_ng_main_flags(&dummy_parser, &key, val, flags);
}

/**
 * Parse flags from bencode string into given bencode dictionary.
 *
 * Params:
 * @param rtpp_flags - raw str rtpp_flags
 * @param dict - root dict to store encoded flags
 */
void parse_rtpp_flags(const str * rtpp_flags, sdp_ng_flags *out)
{
	str remainder, key, val;
	unsigned int direction_flag = 0;
	unsigned int transport = 0;

	if (!rtpp_flags->s)
		return;

	remainder = *rtpp_flags;

	while (remainder.len)
	{
		/* skip spaces */
		while (remainder.len && remainder.s[0] == ' ')
			str_shift(&remainder, 1);

		/* set key and val */
		if (!get_key_val(&key, &val, &remainder))
			break;

		/* codecs have own specific parsing as well */
		if (parse_codecs(out, &key, &val))
			goto next;

		/* parse other generic flags */
		switch (key.len)
		{
			case 3:
				/* transport */
				if (!val.s && str_eq(&key, "RTP"))
					transport = (transport | 0x100) & ~0x001;
				else if (!val.s && str_eq(&key, "AVP"))
					transport = (transport | 0x100) & ~0x002;
				/* other non-defined flags */
				else
					goto generic;
				goto next;
				break;
			case 4:
				/* transport */
				if (!val.s && str_eq(&key, "SRTP"))
					transport |= 0x101;
				else if (!val.s && str_eq(&key, "AVPF"))
					transport |= 0x102;
				else if (!val.s && str_eq(&key, "DTLS"))
					transport |= 0x104;
				/* other non-defined flags */
				else
					goto generic;
				goto next;
				break;
			case 7:
				/* transport */
				if (!val.s && str_eq(&key, "RTP/AVP"))
					transport = 0x100;
				/* other non-defined flags */
				else
					goto generic;
				goto next;
				break;
			case 8:
				/* transport */
				if (!val.s && str_eq(&key, "RTP/AVPF"))
					transport = 0x102;
				else if (!val.s && str_eq(&key, "RTP/SAVP"))
					transport = 0x101;
				/* from-tag can be overriden, but originally has to be provided */
				else if (val.s && str_eq(&key, "from-tag")) {
					out->directional = 1; /* explicitly add directional for this case */
					goto generic;
				}
				/* direction */
				else if (str_eq(&key, "internal") || str_eq(&key, "external"))
					rtpp_direction_flag(out, &direction_flag, &key);
				/* other non-defined flags */
				else
					goto generic;
				goto next;
				break;
			case 9:
				/* transport */
				if (!val.s && str_eq(&key, "RTP/SAVPF"))
					transport = 0x103;
				/* direction */
				else if (str_eq(&key, "direction"))
					rtpp_direction_flag(out, &direction_flag, &val);
				else
					goto generic;
				goto next;
				break;
			case 16:
				/* transport */
				if (!val.s && str_eq(&key, "UDP/TLS/RTP/SAVP"))
					transport = 0x104;
				else
					goto generic;
				goto next;
				break;
			case 17:
				/* transport */
				if (!val.s && str_eq(&key, "UDP/TLS/RTP/SAVPF"))
					transport = 0x106;
				else
					goto generic;
				goto next;
				break;
		}

generic:
		/* generic one key flags */
		if (!val.s)
			call_ng_flags_flags(&key, 0, out);
		/* generic flags with value, but no particular processing */
		else
			call_ng_main_flags(&dummy_parser, &key, &val, out);

next:;
	}

	/* define transport */
	if (transport)
		parse_transports(transport, out);
}
