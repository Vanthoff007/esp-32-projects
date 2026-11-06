#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "wifi_portal.h"

// The setup page served on the board's own hotspot: lists the 2.4 GHz networks it can see,
// takes a password, and hands the choice to wifi.c through a queue.
// Pattern from ESP-IDF v6.1 examples/wifi/scan (scanning) and
// examples/protocols/http_server/simple (handlers, POST body).

static const char *TAG = "portal";

#define MAX_APS 20

static httpd_handle_t s_server;
static QueueHandle_t s_creds_queue;   // holds at most one submitted login
static wifi_ap_record_t s_aps[MAX_APS];
static uint16_t s_ap_count;
static char s_status[160];
static volatile bool s_busy;          // volatile: written by one task, read by another

static const char *PAGE_HEAD =
    "<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Night Sentinel setup</title><style>"
    "body{font-family:sans-serif;max-width:420px;margin:16px auto;padding:0 16px}"
    "label{display:block;padding:10px;border-bottom:1px solid #ddd}"
    "small{color:#666}input[type=password]{width:100%;padding:8px;box-sizing:border-box}"
    "button{margin-top:12px;padding:10px 20px}.status{background:#fee;padding:10px}"
    "</style></head><body><h2>Night Sentinel Wi-Fi</h2>";

static const char *CONNECTING_HTML =
    "<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<meta http-equiv=refresh content='25;url=/'></head>"
    "<body style='font-family:sans-serif;max-width:420px;margin:16px auto;padding:0 16px'>"
    "<h2>Connecting...</h2><p>If it works, this hotspot disappears and the board's LED turns on.</p>"
    "<p>If not, this page reloads in 25 s with the reason. Your phone may drop off the hotspot "
    "for a moment; if it does, rejoin it and open http://192.168.4.1/ again.</p></body></html>";

// qsort() calls this to compare two scan results: stronger signal (higher RSSI) first.
static int by_rssi(const void *a, const void *b)
{
    const wifi_ap_record_t *x = a, *y = b;
    return y->rssi - x->rssi;
}

static void scan(void)
{
    if (s_busy) {
        return;
    }
    wifi_scan_config_t scan_cfg = { .show_hidden = false };
    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);   // true = block until done (~2 s)
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan failed: %s", esp_err_to_name(err));
        return;
    }
    uint16_t n = MAX_APS;   // in: room in s_aps, out: how many were filled
    esp_wifi_scan_get_ap_records(&n, s_aps);
    qsort(s_aps, n, sizeof(s_aps[0]), by_rssi);
    s_ap_count = n;
    ESP_LOGI(TAG, "scan found %u networks", n);
}

// Network names come from anyone nearby, so escape them before putting them in HTML.
// Otherwise an SSID like <script> would run on the page.
static void html_escape(const char *in, char *out, size_t out_size)
{
    size_t n = 0;
    for (; *in; in++) {
        const char *rep;
        char one[2] = { *in, '\0' };
        switch (*in) {
        case '<':  rep = "&lt;";   break;
        case '>':  rep = "&gt;";   break;
        case '&':  rep = "&amp;";  break;
        case '"':  rep = "&quot;"; break;
        case '\'': rep = "&#39;";  break;
        default:   rep = one;      break;
        }
        size_t len = strlen(rep);
        if (n + len >= out_size) {
            break;   // no room left: cut it short rather than overflow
        }
        memcpy(out + n, rep, len);
        n += len;
    }
    out[n] = '\0';
}

// Form values arrive URL-encoded: a space as '+', other symbols as %XX (two hex digits).
// Returns the decoded length, or -1 if it doesn't fit in out.
static int url_decode(const char *in, char *out, size_t out_size)
{
    size_t n = 0;
    while (*in) {
        char ch;
        if (*in == '+') {
            ch = ' ';
            in++;
        } else if (in[0] == '%' && isxdigit((unsigned char)in[1]) && isxdigit((unsigned char)in[2])) {
            char hex[3] = { in[1], in[2], '\0' };
            ch = (char)strtol(hex, NULL, 16);
            in += 3;
        } else {
            ch = *in++;
        }
        if (n + 1 >= out_size) {
            return -1;   // no room for ch plus the closing '\0'
        }
        out[n++] = ch;
    }
    out[n] = '\0';
    return (int)n;
}

static esp_err_t redirect_home(httpd_req_t *req)
{
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

// GET / : the network list and password form. Sent in pieces (chunks), so the whole page
// never has to fit in one buffer.
static esp_err_t index_handler(httpd_req_t *req)
{
    char esc[200];
    char line[600];   // room for the escaped name twice plus the HTML around it

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, PAGE_HEAD);

    if (s_status[0] != '\0') {
        char status_esc[sizeof(s_status) * 6];
        html_escape(s_status, status_esc, sizeof(status_esc));
        httpd_resp_sendstr_chunk(req, "<p class=status>");
        httpd_resp_sendstr_chunk(req, status_esc);
        httpd_resp_sendstr_chunk(req, "</p>");
    }

    httpd_resp_sendstr_chunk(req, "<form method=post action=/save><p>Pick your network:</p>");
    int shown = 0;
    for (int i = 0; i < s_ap_count; i++) {
        const char *ssid = (const char *)s_aps[i].ssid;
        if (ssid[0] == '\0') {
            continue;
        }
        // One router can show up several times (mesh/extenders). The list is sorted strongest
        // first, so skip any later copy of a name we've already listed.
        bool dup = false;
        for (int j = 0; j < i; j++) {
            if (strcmp(ssid, (const char *)s_aps[j].ssid) == 0) {
                dup = true;
                break;
            }
        }
        if (dup) {
            continue;
        }
        bool open = s_aps[i].authmode == WIFI_AUTH_OPEN || s_aps[i].authmode == WIFI_AUTH_WEP;
        html_escape(ssid, esc, sizeof(esc));
        snprintf(line, sizeof(line),
                 "<label><input type=radio name=ssid value=\"%s\"%s> %s "
                 "<small>%d dBm%s</small></label>",
                 esc, open ? " disabled" : "",
                 esc, s_aps[i].rssi, open ? ", open/WEP: not allowed" : "");
        httpd_resp_sendstr_chunk(req, line);
        shown++;
    }
    if (shown == 0) {
        httpd_resp_sendstr_chunk(req, "<p>No 2.4 GHz networks found. The board can't see 5 GHz.</p>");
    }
    // A network that isn't on air right now can be typed in. That's the only way to set up a
    // phone's own hotspot from that phone: it can't share a hotspot while it's on this Wi-Fi.
    httpd_resp_sendstr_chunk(req,
        "<p>Not in the list? Type its name:<br>"
        "<input name=other maxlength=32 style='width:100%;padding:8px;box-sizing:border-box'></p>"
        "<p><small>Setting up this phone's own hotspot? Type its name and password, press Connect, "
        "then leave this Wi-Fi and turn the hotspot on. The board keeps trying every 30 s.</small></p>"
        "<p>Password:<br><input type=password name=pass minlength=8 maxlength=64 required></p>"
        "<button>Connect</button></form>"
        "<p><a href=/rescan>Scan again</a> <small>(signal: -50 dBm great, -80 dBm weak)</small></p>"
        "</body></html>");
    return httpd_resp_sendstr_chunk(req, NULL);   // NULL = last chunk, page done
}

// POST /save : body looks like "ssid=Airtel_Connect&pass=my+secret".
static esp_err_t save_handler(httpd_req_t *req)
{
    char body[400];
    if (req->content_len >= sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "form too long");
        return ESP_FAIL;
    }
    size_t got = 0;
    while (got < req->content_len) {
        int r = httpd_req_recv(req, body + got, req->content_len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;   // slow phone, keep waiting
        }
        if (r <= 0) {
            return ESP_FAIL;   // connection dropped
        }
        got += r;
    }
    body[got] = '\0';

    ns_wifi_creds_t creds = {0};
    char raw[200];   // still URL-encoded: each character can take up to 3 bytes (%XX)
    // A typed name wins over the list; otherwise use the radio button.
    bool have_ssid =
        (httpd_query_key_value(body, "other", raw, sizeof(raw)) == ESP_OK &&
         url_decode(raw, creds.ssid, sizeof(creds.ssid)) > 0) ||
        (httpd_query_key_value(body, "ssid", raw, sizeof(raw)) == ESP_OK &&
         url_decode(raw, creds.ssid, sizeof(creds.ssid)) > 0);
    if (!have_ssid) {
        ns_portal_set_status("Pick a network from the list, or type its name.");
        return redirect_home(req);
    }
    int pass_len = -1;
    if (httpd_query_key_value(body, "pass", raw, sizeof(raw)) == ESP_OK) {
        pass_len = url_decode(raw, creds.pass, sizeof(creds.pass));
    }
    if (pass_len < 8) {
        ns_portal_set_status("The password must be 8 to 64 characters.");
        return redirect_home(req);
    }
    if (s_busy) {
        ns_portal_set_status("Still busy with the last attempt. Try again in a few seconds.");
        return redirect_home(req);
    }

    ESP_LOGI(TAG, "got a login for \"%s\" from the setup page", creds.ssid);
    xQueueOverwrite(s_creds_queue, &creds);   // wifi.c picks it up in ns_portal_wait_creds()
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_sendstr(req, CONNECTING_HTML);
}

// GET /rescan : scan again, then show the list.
static esp_err_t rescan_handler(httpd_req_t *req)
{
    scan();
    return redirect_home(req);
}

void ns_portal_start(void)
{
    if (s_creds_queue == NULL) {
        s_creds_queue = xQueueCreate(1, sizeof(ns_wifi_creds_t));
    }
    s_status[0] = '\0';
    scan();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 6144;   // index_handler keeps ~1.6 KB of text buffers on the stack
    ESP_ERROR_CHECK(httpd_start(&s_server, &config));

    const httpd_uri_t pages[] = {
        { .uri = "/",       .method = HTTP_GET,  .handler = index_handler },
        { .uri = "/save",   .method = HTTP_POST, .handler = save_handler },
        { .uri = "/rescan", .method = HTTP_GET,  .handler = rescan_handler },
    };
    for (size_t i = 0; i < sizeof(pages) / sizeof(pages[0]); i++) {
        httpd_register_uri_handler(s_server, &pages[i]);
    }
}

void ns_portal_stop(void)
{
    if (s_server != NULL) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}

bool ns_portal_wait_creds(ns_wifi_creds_t *out, uint32_t timeout_ms)
{
    return xQueueReceive(s_creds_queue, out, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void ns_portal_set_status(const char *msg)
{
    snprintf(s_status, sizeof(s_status), "%s", msg);
}

void ns_portal_set_busy(bool busy)
{
    s_busy = busy;
}
