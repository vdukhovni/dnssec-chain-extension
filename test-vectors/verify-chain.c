#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <getdns/getdns.h>
#include <getdns/getdns_extra.h>

void print_usage(FILE *out, const char *progname)
{
	fprintf( out
	       , "usage: %s [-t <verify date>] <trust anchor file> "
	         "<chain file> <domain name> <port> [<expected dnssec status>]"
	         "\n\twhere <verify date> is YYYYMMDD[hhmmss]\n"
	       , progname);
}

int main(int argc, char **argv)
{
	FILE *ta_file = NULL;
	getdns_list *tas = NULL;
	getdns_return_t r = GETDNS_RETURN_GENERIC_ERROR;
	FILE *chain_file = NULL;
	getdns_list *chain = NULL;
	getdns_list *to_validate = NULL;
	getdns_list *support = NULL;
	getdns_list *answer = NULL;
	getdns_dict *request = NULL;
	size_t chain_len, i;
	getdns_return_t dnssec_status;
	char qname_str[1024];
	getdns_bindata *qname;
	getdns_return_t expected_dnssec_status = GETDNS_DNSSEC_SECURE;
	time_t validation_time = time(NULL); /* now */
	int opt;
	struct tm tm;
	const char *progname = argv[0];

	while ((opt = getopt(argc, argv, "ht:")) != -1) {
		switch (opt) {
		case 't':
			memset(&tm, 0, sizeof(tm));
			if (strlen(optarg) == 8
			&&  sscanf(optarg, "%4d%2d%2d", &tm.tm_year
			                              , &tm.tm_mon
			                              , &tm.tm_mday)) {
				tm.tm_year -= 1900;
				tm.tm_mon -= 1;
				validation_time = mktime(&tm);

			} else if (strlen(optarg) == 14
			&&  sscanf(optarg, "%4d%2d%2d%2d%2d%2d"
			                 , &tm.tm_year, &tm.tm_mon, &tm.tm_mday
			                 , &tm.tm_hour, &tm.tm_min, &tm.tm_sec)) {
				tm.tm_year -= 1900;
				tm.tm_mon -= 1;
				validation_time = mktime(&tm);

			} else
				validation_time = (time_t)atol(optarg);
			break;
		case 'h':
			print_usage(stdout, progname);
			exit(EXIT_SUCCESS);
		default:
			print_usage(stderr, progname);
			exit(EXIT_FAILURE);
		}
	}
	argc -= optind;
	argv += optind;
	if (argc == 5)
		expected_dnssec_status = (getdns_return_t)atoi(argv[4]);

	if (argc != 4 && argc != 5)
		print_usage(stderr, progname);

	else if (snprintf( qname_str, sizeof(qname_str), "_%s._tcp.%s."
	                 , argv[3], argv[2]) < 0)
		fprintf(stderr, "Problem with snprinf\n");

	else if (!(support = getdns_list_create()) ||
	    !(answer = getdns_list_create()) ||
	    !(to_validate = getdns_list_create()))
		fprintf(stderr, "Error creating list\n");

	else if (!(request = getdns_dict_create()))
		fprintf(stderr, "Error creating dict\n");

	else if (!(ta_file = fopen(argv[0], "r")))
		perror("Error opening trust anchor file");

	else if (!(chain_file = fopen(argv[1], "r")))
		perror("Error opening chain file");

	else if ((r = getdns_str2bindata(qname_str, &qname)))
		fprintf(stderr, "Cannot make qname from \"%s\"", qname_str);

	else if ((r = getdns_fp2rr_list(ta_file, &tas, NULL, 0))) 
		fprintf(stderr, "Error reading trust anchor file");

	else if ((r = getdns_fp2rr_list(chain_file, &chain, NULL, 0))) 
		fprintf(stderr, "Error reading chain file");

	else if ((r = getdns_list_get_length(chain, &chain_len))) 
		fprintf(stderr, "Error getting length of chain");

	else for (i = 0; i < chain_len; i++) {
		getdns_dict *rr;
		uint32_t rr_type;
		getdns_list *append = NULL;
		size_t a;

		if ((r = getdns_list_get_dict(chain, i, &rr)) ||
		    (r = getdns_dict_get_int(rr, "type", &rr_type))) {
			fprintf(stderr, "Error getting RR type");
			break;
		}
		if (rr_type == GETDNS_RRTYPE_RRSIG &&
		    (r = getdns_dict_get_int(rr, "/rdata/type_covered"
		                               , &rr_type))) {
			fprintf(stderr, "Error getting covered RR type");
			break;
		}
		append = (rr_type == GETDNS_RRTYPE_DNSKEY ||
		          rr_type == GETDNS_RRTYPE_DS) ? support : answer;
	
		if ((r = getdns_list_get_length(append, &a)) ||
		    (r = getdns_list_set_dict(append, a, rr))) {
			fprintf(stderr, "Error appending RR");
			break;
		}
	}
	if (r != GETDNS_RETURN_GOOD) ; /* pass */
	else if ((r = getdns_dict_set_bindata(request, "/question/qname", qname)) ||
	    (r = getdns_dict_set_int(request, "/question/qtype", GETDNS_RRTYPE_TLSA)) ||
	    (r = getdns_dict_set_int(request, "/question/qclass", GETDNS_RRCLASS_IN)))
		fprintf(stderr, "Error setting question");

	else if ((r = getdns_dict_set_list(request, "answer", answer)))
		fprintf(stderr, "Error setting answer");

	else if ((r = getdns_list_set_dict(to_validate, 0, request)))
		fprintf(stderr, "Error setting request");

	else if ((dnssec_status = getdns_validate_dnssec2(to_validate, support,
	    tas, validation_time, 3600)) != expected_dnssec_status) {
		fprintf(stderr, "Chain did not validate");
		r = dnssec_status;

	} else {
		uint8_t buf[8192], *ptr = buf;
		int buf_sz = sizeof(buf);

		*ptr++ = 0;
		*ptr++ = 0;

		for (i = 0; i < chain_len; i++) {
			getdns_dict  *rr;

			(void) getdns_list_get_dict(chain, i, &rr);
			if ((r = getdns_rr_dict2wire_scan(rr, &ptr, &buf_sz))) {
				fprintf(stderr, "Error converting to writeformat");
				break;
			}
		}
		if (!r) for (i = 0; i < ptr - buf; i++) {
			if (i % 16 == 0) {
				if (i > 0)
					printf("\n");
				printf("%.4x: ", (int)i);
			} else if (i % 8 == 0)
				printf(" ");
			printf(" %.2x", (int)buf[i]);
		}
		printf("\n");
	}
	if (support) getdns_list_destroy(support);
	if (chain_file) fclose(chain_file);
	if (tas) getdns_list_destroy(tas);
	if (ta_file) fclose(ta_file);
	if (to_validate) getdns_list_destroy(to_validate);
	if (r) {
		if (r != GETDNS_RETURN_GENERIC_ERROR) {
			fprintf(stderr, ": %s\n", getdns_get_errorstr_by_id(r));
		}
		exit(EXIT_FAILURE);
	}
	return 0;
}
