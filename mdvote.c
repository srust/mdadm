#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <limits.h>
#include <curl/curl.h>
#include <uuid/uuid.h>
#include <errno.h>
#include "mdvote.h"

#ifndef MAX 
#define MAX(a, b) ( (a) > (b) ? (a) : (b) )
#endif
#ifndef MIN 
#define MIN(a, b) ( (a) < (b) ? (a) : (b) )
#endif

// dprintf from mdadm.h, avoiding including the whole thing
#ifndef dprintf
#define DEBUG 1
#ifdef DEBUG
extern char Name[];
#define dprintf(fmt, arg...) \
	fprintf(stderr, "%s: %s: "fmt, Name, __func__, ##arg)
#define dprintf_cont(fmt, arg...) \
	fprintf(stderr, fmt, ##arg)
#else
#define dprintf(fmt, arg...) \
        ({ if (0) fprintf(stderr, "%s: %s: " fmt, Name, __func__, ##arg); 0; })
#define dprintf_cont(fmt, arg...) \
        ({ if (0) fprintf(stderr, fmt, ##arg); 0; })
#endif
#endif // dprintf

#define COMPILE_STRLEN(s) (((sizeof s)/(sizeof s[0])) - (sizeof s[0]))
static const char mdvote_endpoint[] = "http://localhost:27003/event-counts";
static char mdvote_origin[128] = { };
    
typedef char uuid_string_t[37];
typedef struct curlbuf_s {
    char buf[4096];
    int  ofs;
    int  len;
} curlbuf;
  
////////////////////////////////////////////////////////////////////////////////
/// gettimeofday() in microseconds in one 64-bit int
///
/// A signed integer is used.  Sure, this only affords us 292 thousand years...
////////////////////////////////////////////////////////////////////////////////
static int64_t
mdvote_time64_us(void)
{
    struct timeval tv;

    gettimeofday(&tv, 0);

    return (int64_t)tv.tv_sec * 1000000L + (int64_t)tv.tv_usec;
}

////////////////////////////////////////////////////////////////////////////////
/// Generate a string representation of the array UUID
///
/// The UUID is stored in big-endian format in the metadata.
////////////////////////////////////////////////////////////////////////////////
static void
mdvote_uuid_unparse(const uuid_t uu_in, char *out)
{
    uuid_t   uu;
    int32_t *suu_in = (void *)&uu_in[0];
    int32_t *suu    = (void *)&uu[0];
    int      i;

    for (i = 0; i < 4; i++) {
        suu[i] = be32toh(suu_in[i]);
    }

    uuid_unparse(uu, out);
}

////////////////////////////////////////////////////////////////////////////////
/// Type-to-string definition
///
/// Non-volatile - used in persistent storage.
////////////////////////////////////////////////////////////////////////////////
static const char *
mdvote_type_key(mdvote_type type)
{
    switch (type) {
    case MDVOTE_INVAL:
        return "inval";
    case MDVOTE_ASSEMBLY:
        return "assembly";
    case MDVOTE_MEMBER:
        return "member";
    }
    return "inval";
}

////////////////////////////////////////////////////////////////////////////////
/// Callback from libcurl for PUT data
////////////////////////////////////////////////////////////////////////////////
static size_t
mdvote_put_cb(void *out, size_t size, size_t nmemb, void *arg)
{
    curlbuf *cb  = arg;
    int      eat = size * nmemb;

    eat = MIN(cb->len - cb->ofs, eat);
    if (eat < 0) {
        dprintf("ofs:%d len:%d eat:%d\n", cb->ofs, cb->len, eat);
        eat = 0;
    }
    else if (eat > 0) {
        memcpy(out, &cb->buf[cb->ofs], eat);
        cb->ofs += eat;
    }
    
    return eat;
}

////////////////////////////////////////////////////////////////////////////////
/// Callback from libcurl to receive data
////////////////////////////////////////////////////////////////////////////////
static size_t
mdvote_recv_cb(void *in, size_t size, size_t nmemb, void *arg)
{
    curlbuf *cb  = arg;
    int      eat = size * nmemb;

    if (cb->ofs + eat > (int)sizeof cb->buf)
        eat -= cb->ofs + eat - (int)sizeof cb->buf;

    memcpy(&cb->buf[cb->ofs], in, eat);
    cb->ofs += eat;
    cb->len  = cb->ofs;

    return eat;
}

////////////////////////////////////////////////////////////////////////////////
/// Log HTTP/libcurl results
////////////////////////////////////////////////////////////////////////////////
static void
mdvote_log_res(const char *verb, const char *url_in, CURLcode cres,
               long http_code, const char *errbuf, const curlbuf *req,
               const curlbuf *rsp, int64_t start_us)
{
    static int slen = COMPILE_STRLEN(mdvote_endpoint);
    const char *url;
    
    if (strncmp(url_in, mdvote_endpoint, slen) == 0)
        url = &url_in[slen];
    else
        url = url_in;

    dprintf("\"%s %s 1.1\" %ld %d [%0.2fms]\n", verb, url, http_code, rsp->ofs,
            ((double)mdvote_time64_us() - start_us) / 1000.0f);

    if (cres != CURLE_OK) {
        size_t errlen = errbuf ? strlen(errbuf) : 0;
        if(errlen)
            dprintf("%s%s", errbuf, ((errbuf[errlen - 1] != '\n') ? "\n" : ""));
        else
            dprintf("%s\n", curl_easy_strerror(cres));
    }
    if (rsp->ofs > 0)
        dprintf("%s\n", rsp->buf);
}


////////////////////////////////////////////////////////////////////////////////
/// PUBLIC INTERFACES
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// Retrieve the vote sequence number for the specified UUID
////////////////////////////////////////////////////////////////////////////////
int64_t
mdvote_get(const unsigned char uuid[16], mdvote_type type)
{
    CURLcode       cres;
    curlbuf        rsp;
    char           errbuf[CURL_ERROR_SIZE];
    char           url[256];
    uuid_string_t  uuidstr;
    int            len;
    char          *s;
    long           http_code = 0;
    int64_t        start_us  = mdvote_time64_us();

    errbuf[0] = '\0';
    memset(&rsp, 0, sizeof rsp);

    CURL *c = curl_easy_init();
    if (!c) {
        perror("curl_easy_init");
        return -1L;
    }

    mdvote_uuid_unparse(uuid, uuidstr);

    len = snprintf(url, sizeof url, "%s/%s.%s", mdvote_endpoint,
                   uuidstr, mdvote_type_key(type));
    if (len >= (int)sizeof url) {
        dprintf("url too long: '%s'\n", url);
        return -1L;
    }

    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, mdvote_recv_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &rsp);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, errbuf);

    cres = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(c);

    mdvote_log_res("GET", url, cres, http_code, errbuf, NULL, &rsp, start_us);    
    
    if (cres != CURLE_OK)
        return -EAGAIN;
    else if (http_code != 200) {
        switch (http_code) {
        case 404: // not found
        case 410: // gone
            return -ENOENT;
        default:
            return -EAGAIN;
        }
    }

    s = NULL;
    rsp.buf[rsp.ofs] = '\0';
    dprintf("%s vote sequence %s\n", uuidstr, rsp.buf);

    long v = strtol(rsp.buf, &s, 10);
    if (v == LONG_MIN || v == LONG_MAX) {
        dprintf("failed to parse sequence: %s\n", rsp.buf);
        return -1L;
    }

    return v;
}

int
mdvote_put(const unsigned char uuid[16], mdvote_type type, int64_t v)
{
    CURLcode           cres;
    struct curl_slist *headers   = NULL;
    curlbuf            rsp;
    char               errbuf[CURL_ERROR_SIZE];
    char               url[256];
    uuid_string_t      uuidstr;
    int                len;
    long               http_code = 0;
    int64_t            start_us  = mdvote_time64_us();

    errbuf[0] = '\0';
    memset(&rsp, 0, sizeof rsp);

    CURL *c = curl_easy_init();
    if (!c) {
        perror("curl_easy_init");
        return -1L;
    }

    mdvote_uuid_unparse(uuid, uuidstr);

    len = snprintf(url, sizeof url, "%s/%s.%s", mdvote_endpoint,
                   uuidstr, mdvote_type_key(type));
    if (len >= (int)sizeof url) {
        dprintf("url too long: '%s'\n", url);
        return -1L;
    }

    curlbuf req;
    memset(&req, 0, sizeof req);
    len = snprintf(req.buf, sizeof req.buf,
                   "{ \"value\":%ld, \"origin\":\"%s\" }\n", v, mdvote_origin);

    req.len = strlen(req.buf);

    // "easy"
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_UPLOAD, 1);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, mdvote_recv_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &rsp);
    curl_easy_setopt(c, CURLOPT_READFUNCTION, mdvote_put_cb);
    curl_easy_setopt(c, CURLOPT_READDATA, &req);
    curl_easy_setopt(c, CURLOPT_INFILESIZE_LARGE, (curl_off_t)req.len);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, errbuf);
    
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Expect:");
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);

    cres = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(c);
    curl_slist_free_all(headers);
    headers = NULL;

    mdvote_log_res("PUT", url, cres, http_code, errbuf, &req, &rsp, start_us);
    
    if (cres != CURLE_OK)
        return -EAGAIN;
    else if (http_code != 200) {
        switch (http_code) {
        case 404: // not found
        case 410: // gone
            return -ENOENT;
        default:
            return -EAGAIN;
        }
    }

    return 0;
    
}

////////////////////////////////////////////////////////////////////////////////
/// Read /etc/hostname into mdvote_origin
////////////////////////////////////////////////////////////////////////////////
void
mdvote_init(void)
{
    FILE *fp;
    char *s;
    
    fp = fopen("/etc/hostname", "r");
    if (fp) {
        if (fgets(mdvote_origin, sizeof mdvote_origin, fp) == NULL)
            mdvote_origin[0] = '\0';
        fclose(fp);
    }

    for (s = mdvote_origin; *s != '\0'; s++) {
        switch (*s) {
        case '\b':
        case '\n':
        case '\r':
        case '\t':
        case '"':
        case '\\':
        case '/':
        case ' ':
            *s = '-';
        }
    }

    // remove trailing junk
    for (s = s - 1; s != mdvote_origin; s--) {
        if (*s == '-')
            *s = '\0';
        else
            break;
    }
}
