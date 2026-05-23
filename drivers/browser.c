#include <stdint.h>
#include "browser.h"
#include "window.h"
#include "graphics.h"
#include "net.h"

#define BROWSER_HOME_URL "https;//example.com/"

static browser_t g_browser;

static void browser_set_url(browser_t *browser, const char *url) {
    uint16_t i = 0;
    const char *src = (url && url[0]) ? url : "example.com/";
    while (src[i] && (uint32_t)(i + 1) < sizeof(browser->url)) {
        browser->url[i] = src[i];
        i++;
    }
    browser->url[i] = 0;
}

static void browser_reset_source_scroll(browser_t *browser) {
    if (browser) browser->source_offset = 0;
}

static void browser_clear_source_search(browser_t *browser) {
    if (!browser) return;
    browser->source_searching = 0;
    browser->source_query[0] = 0;
}

static uint16_t browser_url_len(const browser_t *browser) {
    uint16_t len = 0;
    while (browser && browser->url[len]) len++;
    return len;
}

static uint16_t browser_source_query_len(const browser_t *browser) {
    uint16_t len = 0;
    while (browser && browser->source_query[len]) len++;
    return len;
}

static void browser_copy(char *dst, uint32_t cap, const char *src) {
    uint32_t i = 0;
    if (!cap) return;
    while (src && src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void browser_remember_current(browser_t *browser) {
    if (!browser || !browser->url[0]) return;
    browser_copy(browser->history_url, sizeof(browser->history_url), browser->url);
    browser->has_history = 1;
}

static char lower_char(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static int browser_starts_with(const char *text, const char *prefix) {
    while (*prefix) {
        if (lower_char(*text++) != *prefix++) return 0;
    }
    return 1;
}

static int browser_is_https(const char *url) {
    return browser_starts_with(url, "https://") ||
           browser_starts_with(url, "https;//");
}

static int match_word(const uint8_t *data, uint16_t index, uint16_t len, const char *word) {
    while (*word && index < len) {
        if (lower_char((char)data[index++]) != *word++) return 0;
    }
    return *word == 0;
}

static int match_tag_name(const uint8_t *data, uint16_t index, uint16_t len, const char *name) {
    uint16_t i = index;
    if (!match_word(data, i, len, name)) return 0;
    while (*name) {
        i++;
        name++;
    }
    if (i >= len) return 1;
    char c = lower_char((char)data[i]);
    return c == '>' || c == '/' || c == ' ' || c == '\r' || c == '\n' || c == '\t';
}

static int is_space(char c) {
    return c == ' ' || c == '\r' || c == '\n' || c == '\t';
}

static uint16_t find_tag_end(const uint8_t *data, uint16_t index, uint16_t len) {
    while (index < len && data[index] != '>') index++;
    return index;
}

static uint16_t skip_tag(const uint8_t *data, uint16_t index, uint16_t len) {
    index = find_tag_end(data, index, len);
    return index < len ? (uint16_t)(index + 1) : index;
}

static int css_name_boundary(const uint8_t *data, uint16_t index, uint16_t start) {
    if (index == start) return 1;
    char c = (char)data[index - 1];
    return c == '{' || c == ';' || is_space(c);
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = lower_char(c);
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    return -1;
}

static int parse_hex_color(const uint8_t *data, uint16_t index, uint16_t end, uint32_t *out) {
    if (index >= end || data[index] != '#') return 0;
    if ((uint16_t)(index + 3) < end) {
        int r = hex_value((char)data[index + 1]);
        int g = hex_value((char)data[index + 2]);
        int b = hex_value((char)data[index + 3]);
        if (r >= 0 && g >= 0 && b >= 0 &&
            ((uint16_t)(index + 4) >= end || hex_value((char)data[index + 4]) < 0)) {
            *out = graphics_rgb((uint8_t)(r * 17), (uint8_t)(g * 17), (uint8_t)(b * 17));
            return 1;
        }
    }
    if ((uint16_t)(index + 6) < end) {
        int r1 = hex_value((char)data[index + 1]);
        int r2 = hex_value((char)data[index + 2]);
        int g1 = hex_value((char)data[index + 3]);
        int g2 = hex_value((char)data[index + 4]);
        int b1 = hex_value((char)data[index + 5]);
        int b2 = hex_value((char)data[index + 6]);
        if (r1 >= 0 && r2 >= 0 && g1 >= 0 && g2 >= 0 && b1 >= 0 && b2 >= 0) {
            *out = graphics_rgb((uint8_t)((r1 << 4) | r2),
                                (uint8_t)((g1 << 4) | g2),
                                (uint8_t)((b1 << 4) | b2));
            return 1;
        }
    }
    return 0;
}

static int parse_named_color(const uint8_t *data, uint16_t index, uint16_t end, uint32_t *out) {
    if (match_word(data, index, end, "black")) *out = graphics_rgb(0, 0, 0);
    else if (match_word(data, index, end, "white")) *out = graphics_rgb(255, 255, 255);
    else if (match_word(data, index, end, "red")) *out = graphics_rgb(180, 20, 20);
    else if (match_word(data, index, end, "green")) *out = graphics_rgb(20, 130, 55);
    else if (match_word(data, index, end, "blue")) *out = graphics_rgb(25, 75, 180);
    else if (match_word(data, index, end, "yellow")) *out = graphics_rgb(235, 210, 45);
    else if (match_word(data, index, end, "gray")) *out = graphics_rgb(128, 128, 128);
    else if (match_word(data, index, end, "grey")) *out = graphics_rgb(128, 128, 128);
    else if (match_word(data, index, end, "navy")) *out = graphics_rgb(20, 45, 120);
    else if (match_word(data, index, end, "purple")) *out = graphics_rgb(120, 55, 150);
    else return 0;
    return 1;
}

static int parse_css_color_value(const uint8_t *data, uint16_t index, uint16_t end, uint32_t *out) {
    while (index < end && is_space((char)data[index])) index++;
    if (parse_hex_color(data, index, end, out)) return 1;
    return parse_named_color(data, index, end, out);
}

static int parse_css_property(const uint8_t *data,
                              uint16_t start,
                              uint16_t end,
                              const char *property,
                              uint32_t *out) {
    for (uint16_t i = start; i < end; i++) {
        if (!css_name_boundary(data, i, start) ||
            !match_word(data, i, end, property)) {
            continue;
        }
        uint16_t j = i;
        const char *p = property;
        while (*p) {
            j++;
            p++;
        }
        while (j < end && is_space((char)data[j])) j++;
        if (j >= end || data[j] != ':') continue;
        j++;
        if (parse_css_color_value(data, j, end, out)) return 1;
    }
    return 0;
}

static int tag_style_property(const uint8_t *data,
                              uint16_t start,
                              uint16_t end,
                              const char *property,
                              uint32_t *out) {
    for (uint16_t i = start; i + 5 < end; i++) {
        if (!match_word(data, i, end, "style")) continue;
        i = (uint16_t)(i + 5);
        while (i < end && is_space((char)data[i])) i++;
        if (i >= end || data[i] != '=') continue;
        i++;
        while (i < end && is_space((char)data[i])) i++;
        char quote = 0;
        if (i < end && (data[i] == '"' || data[i] == '\'')) quote = (char)data[i++];
        uint16_t value_start = i;
        while (i < end && ((quote && data[i] != quote) ||
                           (!quote && !is_space((char)data[i])))) {
            i++;
        }
        if (parse_css_property(data, value_start, i, property, out)) return 1;
    }
    return 0;
}

static int tag_attr_value(const uint8_t *data,
                          uint16_t start,
                          uint16_t end,
                          const char *attr,
                          char *out,
                          uint32_t cap);

static int tag_href_value(const uint8_t *data,
                          uint16_t start,
                          uint16_t end,
                          char *out,
                          uint32_t cap) {
    return tag_attr_value(data, start, end, "href", out, cap);
}

static int tag_attr_value(const uint8_t *data,
                          uint16_t start,
                          uint16_t end,
                          const char *attr,
                          char *out,
                          uint32_t cap) {
    if (!cap) return 0;
    out[0] = 0;
    for (uint16_t i = start; i < end; i++) {
        const char *a = attr;
        uint16_t j = i;
        while (*a && j < end && lower_char((char)data[j]) == *a) {
            j++;
            a++;
        }
        if (*a) continue;
        if (i > start) {
            char before = lower_char((char)data[i - 1]);
            if (before != ' ' && before != '\r' && before != '\n' && before != '\t') continue;
        }
        i = j;
        while (i < end && is_space((char)data[i])) i++;
        if (i >= end || data[i] != '=') continue;
        i++;
        while (i < end && is_space((char)data[i])) i++;
        char quote = 0;
        if (i < end && (data[i] == '"' || data[i] == '\'')) quote = (char)data[i++];
        uint32_t o = 0;
        while (i < end && o + 1 < cap &&
               ((quote && data[i] != quote) ||
                (!quote && !is_space((char)data[i])))) {
            out[o++] = (char)data[i++];
        }
        out[o] = 0;
        return o > 0;
    }
    return 0;
}

static void browser_current_host_path(const browser_t *browser,
                                      char *host,
                                      uint32_t host_cap,
                                      char *path,
                                      uint32_t path_cap) {
    const char *url = browser->url[0] ? browser->url : "example.com/";
    if (browser_starts_with(url, "http://")) url += 7;
    else if (browser_starts_with(url, "http;//")) url += 7;
    else if (browser_starts_with(url, "https://")) url += 8;
    else if (browser_starts_with(url, "https;//")) url += 8;
    uint32_t i = 0;
    while (url[i] && url[i] != '/' && i + 1 < host_cap) {
        host[i] = url[i];
        i++;
    }
    host[i] = 0;
    if (!host[0]) browser_copy(host, host_cap, "example.com");

    const char *slash = url + i;
    if (*slash == '/') browser_copy(path, path_cap, slash);
    else browser_copy(path, path_cap, "/");
}

static void browser_resolve_href(const browser_t *browser,
                                 const char *href,
                                 char *out,
                                 uint32_t cap) {
    char host[48];
    char path[64];
    uint32_t o = 0;
    if (!href || !href[0]) {
        if (cap) out[0] = 0;
        return;
    }
    if (browser_starts_with(href, "http://") ||
        browser_starts_with(href, "https://")) {
        browser_copy(out, cap, href);
        return;
    }
    browser_current_host_path(browser, host, sizeof(host), path, sizeof(path));
    const char *scheme = browser_is_https(browser->url) ? "https://" : "";
    for (uint32_t i = 0; scheme[i] && o + 1 < cap; i++) out[o++] = scheme[i];
    for (uint32_t i = 0; host[i] && o + 1 < cap; i++) {
        out[o++] = host[i];
    }
    if (href[0] == '/') {
        for (uint32_t i = 0; href[i] && o + 1 < cap; i++) out[o++] = href[i];
    } else if (href[0] == '#') {
        for (uint32_t i = 0; path[i] && o + 1 < cap; i++) out[o++] = path[i];
    } else {
        if (o + 1 < cap) out[o++] = '/';
        for (uint32_t i = 0; href[i] && o + 1 < cap; i++) out[o++] = href[i];
    }
    out[o] = 0;
}

static int header_name_at(const uint8_t *data,
                          uint16_t index,
                          uint16_t end,
                          const char *name) {
    while (*name && index < end) {
        if (lower_char((char)data[index++]) != *name++) return 0;
    }
    return *name == 0 && index < end && data[index] == ':';
}

static int browser_redirect_location(const browser_t *browser,
                                     const net_info_t *info,
                                     char *out,
                                     uint32_t cap) {
    if (!out || !cap) return 0;
    out[0] = 0;
    if (!browser || !info) return 0;
    if (info->http_status != 301 && info->http_status != 302 &&
        info->http_status != 303 && info->http_status != 307 &&
        info->http_status != 308) {
        return 0;
    }

    uint16_t end = info->http_body_offset ? info->http_body_offset : info->http_response_len;
    for (uint16_t i = 0; i + 9 < end; i++) {
        if (i && info->http_response[i - 1] != '\n') continue;
        if (!header_name_at(info->http_response, i, end, "location")) continue;
        i = (uint16_t)(i + 9);
        while (i < end && (info->http_response[i] == ' ' || info->http_response[i] == '\t')) i++;
        char raw[64];
        uint32_t o = 0;
        while (i < end && info->http_response[i] != '\r' &&
               info->http_response[i] != '\n' && o + 1 < sizeof(raw)) {
            raw[o++] = (char)info->http_response[i++];
        }
        raw[o] = 0;
        browser_resolve_href(browser, raw, out, cap);
        return out[0] != 0;
    }
    return 0;
}

static int browser_header_value(const net_info_t *info,
                                const char *name,
                                char *out,
                                uint32_t cap) {
    if (!out || !cap) return 0;
    out[0] = 0;
    if (!info || !name) return 0;

    uint16_t end = info->http_body_offset ? info->http_body_offset : info->http_response_len;
    for (uint16_t i = 0; i + 2 < end; i++) {
        if (i && info->http_response[i - 1] != '\n') continue;
        if (!header_name_at(info->http_response, i, end, name)) continue;
        while (i < end && info->http_response[i] != ':') i++;
        if (i < end) i++;
        while (i < end && (info->http_response[i] == ' ' || info->http_response[i] == '\t')) i++;

        uint32_t o = 0;
        while (i < end && info->http_response[i] != '\r' &&
               info->http_response[i] != '\n' && o + 1 < cap) {
            out[o++] = (char)info->http_response[i++];
        }
        out[o] = 0;
        return o > 0;
    }
    return 0;
}

static int text_equals_ci(const char *a, const char *b) {
    while (*a && *b) {
        if (lower_char(*a++) != lower_char(*b++)) return 0;
    }
    return *a == 0 && *b == 0;
}

static uint16_t skip_element(const uint8_t *data,
                             uint16_t index,
                             uint16_t len,
                             const char *name) {
    index = skip_tag(data, index, len);
    while (index + 3 < len) {
        if (data[index] == '<' &&
            data[index + 1] == '/' &&
            match_tag_name(data, (uint16_t)(index + 2), len, name)) {
            return skip_tag(data, index, len);
        }
        index++;
    }
    return index;
}

static void append_clean_text(char *out,
                              uint32_t cap,
                              uint32_t *len,
                              char c,
                              uint8_t *pending_space) {
    if (!out || !cap || !len) return;
    if (c == '\r' || c == '\n' || c == '\t' || c == ' ') {
        *pending_space = 1;
        return;
    }
    if (c < 32 || c > 126) return;
    if (*pending_space && *len && *len + 1 < cap) {
        out[(*len)++] = ' ';
    }
    *pending_space = 0;
    if (*len + 1 < cap) out[(*len)++] = c;
    out[*len] = 0;
}

static int browser_title_text(const net_info_t *info, char *out, uint32_t cap) {
    if (!out || !cap) return 0;
    out[0] = 0;
    if (!info) return 0;

    for (uint16_t i = 0; i + 7 < info->http_response_len; i++) {
        if (info->http_response[i] != '<' ||
            !match_tag_name(info->http_response, (uint16_t)(i + 1),
                            info->http_response_len, "title")) {
            continue;
        }
        uint16_t index = skip_tag(info->http_response, i, info->http_response_len);
        uint32_t o = 0;
        uint8_t pending_space = 0;
        while (index + 8 < info->http_response_len) {
            if (info->http_response[index] == '<' &&
                info->http_response[index + 1] == '/' &&
                match_tag_name(info->http_response, (uint16_t)(index + 2),
                               info->http_response_len, "title")) {
                break;
            }
            char c = (char)info->http_response[index++];
            if (c == '&') {
                c = ' ';
                if (match_word(info->http_response, index, info->http_response_len, "amp;")) c = '&';
                else if (match_word(info->http_response, index, info->http_response_len, "lt;")) c = '<';
                else if (match_word(info->http_response, index, info->http_response_len, "gt;")) c = '>';
                else if (match_word(info->http_response, index, info->http_response_len, "quot;")) c = '"';
                while (index < info->http_response_len && info->http_response[index] != ';') index++;
                if (index < info->http_response_len) index++;
            }
            append_clean_text(out, cap, &o, c, &pending_space);
        }
        return out[0] != 0;
    }
    return 0;
}

static int browser_meta_description(const net_info_t *info, char *out, uint32_t cap) {
    if (!out || !cap) return 0;
    out[0] = 0;
    if (!info) return 0;

    for (uint16_t i = 0; i + 5 < info->http_response_len; i++) {
        if (info->http_response[i] != '<' ||
            !match_tag_name(info->http_response, (uint16_t)(i + 1),
                            info->http_response_len, "meta")) {
            continue;
        }
        uint16_t start = (uint16_t)(i + 1);
        uint16_t end = find_tag_end(info->http_response, start, info->http_response_len);
        char name[32];
        char content[64];
        if (!tag_attr_value(info->http_response, start, end, "name", name, sizeof(name)) &&
            !tag_attr_value(info->http_response, start, end, "property", name, sizeof(name))) {
            continue;
        }
        if (!text_equals_ci(name, "description") &&
            !text_equals_ci(name, "og:description")) {
            continue;
        }
        if (tag_attr_value(info->http_response, start, end, "content", content, sizeof(content))) {
            browser_copy(out, cap, content);
            return out[0] != 0;
        }
    }
    return 0;
}

static int browser_html_attr(const net_info_t *info,
                             const char *attr,
                             char *out,
                             uint32_t cap) {
    if (!out || !cap) return 0;
    out[0] = 0;
    if (!info || !attr) return 0;

    for (uint16_t i = 0; i + 5 < info->http_response_len; i++) {
        if (info->http_response[i] != '<' ||
            !match_tag_name(info->http_response, (uint16_t)(i + 1),
                            info->http_response_len, "html")) {
            continue;
        }
        uint16_t start = (uint16_t)(i + 1);
        uint16_t end = find_tag_end(info->http_response, start, info->http_response_len);
        return tag_attr_value(info->http_response, start, end, attr, out, cap);
    }
    return 0;
}

static void browser_short_type(char *text) {
    if (!text || !text[0]) return;
    uint16_t last = 0;
    for (uint16_t i = 0; text[i]; i++) {
        if (text[i] == '/' || text[i] == '#') last = (uint16_t)(i + 1);
    }
    if (!last) return;
    uint16_t o = 0;
    while (text[last]) {
        text[o++] = text[last++];
    }
    text[o] = 0;
}

static int find_ci_text(const char *text, const char *needle) {
    if (!text || !needle || !needle[0]) return -1;
    for (uint16_t i = 0; text[i]; i++) {
        uint16_t j = 0;
        while (needle[j] && text[i + j] &&
               lower_char(text[i + j]) == lower_char(needle[j])) {
            j++;
        }
        if (!needle[j]) return i;
    }
    return -1;
}

static void browser_content_summary(const char *in, char *out, uint32_t cap) {
    if (!out || !cap) return;
    out[0] = 0;
    if (!in || !in[0]) return;

    uint32_t o = 0;
    uint16_t start = 0;
    for (uint16_t i = 0; in[i] && in[i] != ';' && in[i] != ' '; i++) {
        if (in[i] == '/') start = (uint16_t)(i + 1);
    }
    for (uint16_t i = start; in[i] && in[i] != ';' && in[i] != ' ' && o + 1 < cap; i++) {
        out[o++] = in[i];
    }
    out[o] = 0;

    int charset = find_ci_text(in, "charset=");
    if (charset < 0) return;
    uint16_t i = (uint16_t)(charset + 8);
    if (o && o + 1 < cap) out[o++] = ' ';
    while (in[i] && in[i] != ';' && in[i] != ' ' && o + 1 < cap) {
        out[o++] = in[i++];
    }
    out[o] = 0;
}

static int browser_meta_http_equiv(const net_info_t *info, char *out, uint32_t cap) {
    if (!out || !cap) return 0;
    out[0] = 0;
    if (!info) return 0;

    for (uint16_t i = 0; i + 5 < info->http_response_len; i++) {
        if (info->http_response[i] != '<' ||
            !match_tag_name(info->http_response, (uint16_t)(i + 1),
                            info->http_response_len, "meta")) {
            continue;
        }
        uint16_t start = (uint16_t)(i + 1);
        uint16_t end = find_tag_end(info->http_response, start, info->http_response_len);
        char equiv[32];
        if (!tag_attr_value(info->http_response, start, end, "http-equiv",
                            equiv, sizeof(equiv)) ||
            !text_equals_ci(equiv, "Content-Type")) {
            continue;
        }
        if (tag_attr_value(info->http_response, start, end, "content", out, cap)) {
            return out[0] != 0;
        }
    }
    return 0;
}

static uint16_t body_start(const net_info_t *info) {
    uint16_t start = info->http_body_offset ? info->http_body_offset : 0;
    for (uint16_t i = start; i + 5 < info->http_response_len; i++) {
        if (info->http_response[i] == '<' &&
            match_tag_name(info->http_response, (uint16_t)(i + 1),
                           info->http_response_len, "body")) {
            return skip_tag(info->http_response, i, info->http_response_len);
        }
    }
    return start;
}

static int body_style_property(const net_info_t *info, const char *property, uint32_t *out) {
    uint16_t start = info->http_body_offset ? info->http_body_offset : 0;
    for (uint16_t i = start; i + 5 < info->http_response_len; i++) {
        if (info->http_response[i] != '<' ||
            !match_tag_name(info->http_response, (uint16_t)(i + 1),
                            info->http_response_len, "body")) {
            continue;
        }
        uint16_t tag_start = (uint16_t)(i + 1);
        uint16_t tag_end = find_tag_end(info->http_response, tag_start,
                                        info->http_response_len);
        return tag_style_property(info->http_response, tag_start, tag_end, property, out);
    }
    return 0;
}

static int style_block_property(const net_info_t *info, const char *property, uint32_t *out) {
    uint16_t start = info->http_body_offset ? info->http_body_offset : 0;
    for (uint16_t i = start; i + 7 < info->http_response_len; i++) {
        if (info->http_response[i] != '<' ||
            !match_tag_name(info->http_response, (uint16_t)(i + 1),
                            info->http_response_len, "style")) {
            continue;
        }
        uint16_t css_start = skip_tag(info->http_response, (uint16_t)(i + 1),
                                      info->http_response_len);
        uint16_t css_end = css_start;
        while (css_end + 3 < info->http_response_len) {
            if (info->http_response[css_end] == '<' &&
                info->http_response[css_end + 1] == '/' &&
                match_tag_name(info->http_response, (uint16_t)(css_end + 2),
                               info->http_response_len, "style")) {
                break;
            }
            css_end++;
        }
        if (parse_css_property(info->http_response, css_start, css_end, property, out)) {
            return 1;
        }
        i = css_end;
    }
    return 0;
}

static uint32_t browser_page_background(const net_info_t *info, uint32_t fallback) {
    uint32_t color = fallback;
    if (body_style_property(info, "background-color", &color)) return color;
    if (body_style_property(info, "background", &color)) return color;
    if (style_block_property(info, "background-color", &color)) return color;
    if (style_block_property(info, "background", &color)) return color;
    return fallback;
}

static uint32_t browser_page_text_color(const net_info_t *info, uint32_t fallback) {
    uint32_t color = fallback;
    if (body_style_property(info, "color", &color)) return color;
    if (style_block_property(info, "color", &color)) return color;
    return fallback;
}

static void draw_num(window_t *win, uint32_t x, uint32_t y, uint32_t n, uint32_t color) {
    char tmp[12];
    int i = 11;
    tmp[i] = 0;
    if (n == 0) {
        window_draw_char(win, x, y, '0', color);
        return;
    }
    while (n > 0 && i > 0) {
        tmp[--i] = (char)('0' + (n % 10));
        n /= 10;
    }
    window_draw_string(win, x, y, tmp + i, color);
}

static void browser_draw_string_limit_bg(window_t *win,
                                         uint32_t x,
                                         uint32_t y,
                                         const char *text,
                                         uint8_t max_chars,
                                         uint32_t fg,
                                         uint32_t bg) {
    uint8_t i = 0;
    while (text && text[i] && i < max_chars) {
        graphics_draw_char(window_get_x(win) + WINDOW_BORDER + x + (uint32_t)i * 8,
                           window_get_y(win) + TITLEBAR_HEIGHT + WINDOW_BORDER + y,
                           text[i], fg, bg);
        i++;
    }
}

static const char *browser_status_label(const char *status) {
    if (!status) return "";
    if (status[0] == 'F') return "OK";
    if (status[0] == 'H') return "HTTP";
    if (status[0] == 'R') return "RDY";
    return status;
}

static const char *browser_display_url(const browser_t *browser) {
    uint16_t len = browser_url_len(browser);
    if (!browser || len <= 18 || !browser->editing_url) {
        return browser && browser->url[0] ? browser->url : "example.com/";
    }
    return browser->url + (len - 18);
}

static uint16_t emit_entity(window_t *win,
                            const uint8_t *data,
                            uint16_t index,
                            uint16_t len,
                            uint32_t *x,
                            uint32_t *y,
                            uint32_t color,
                            uint8_t underline) {
    char out = ' ';
    if (match_word(data, index, len, "amp;")) out = '&';
    else if (match_word(data, index, len, "lt;")) out = '<';
    else if (match_word(data, index, len, "gt;")) out = '>';
    else if (match_word(data, index, len, "quot;")) out = '"';

    window_draw_char(win, *x, *y, out, color);
    if (underline) window_fill_rect(win, *x, (uint32_t)(*y + 8), 8, 1, color);
    *x += 8;
    while (index < len && data[index] != ';') index++;
    return index < len ? (uint16_t)(index + 1) : index;
}

static void browser_track_link(browser_t *browser, uint8_t link_index, uint32_t x, uint32_t y) {
    if (!browser || link_index >= browser->link_count) return;
    if (!browser->link_w[link_index]) {
        browser->link_x[link_index] = x;
        browser->link_y[link_index] = y;
        browser->link_w[link_index] = 8;
        browser->link_h[link_index] = 9;
        return;
    }
    uint32_t min_x = browser->link_x[link_index] < x ? browser->link_x[link_index] : x;
    uint32_t min_y = browser->link_y[link_index] < y ? browser->link_y[link_index] : y;
    uint32_t max_x = (browser->link_x[link_index] + browser->link_w[link_index]) > (x + 8)
                   ? (browser->link_x[link_index] + browser->link_w[link_index]) : (x + 8);
    uint32_t max_y = (browser->link_y[link_index] + browser->link_h[link_index]) > (y + 9)
                   ? (browser->link_y[link_index] + browser->link_h[link_index]) : (y + 9);
    browser->link_x[link_index] = min_x;
    browser->link_y[link_index] = min_y;
    browser->link_w[link_index] = max_x - min_x;
    browser->link_h[link_index] = max_y - min_y;
}

static void browser_newline(uint32_t *x,
                            uint32_t *y,
                            uint32_t left,
                            uint8_t *pending_space,
                            uint8_t extra) {
    if (*x != left || extra) {
        *x = left;
        *y += extra ? 12 : 10;
    }
    *pending_space = 0;
}

static uint16_t browser_draw_text_char(window_t *win,
                                       uint32_t *x,
                                       uint32_t *y,
                                       char c,
                                       uint32_t color,
                                       uint8_t underline) {
    if (c < 32 || c > 126) c = ' ';
    if (*x > 216) {
        *x = 10;
        *y += 10;
    }
    if (*y >= 112) return 0;
    window_draw_char(win, *x, *y, c, color);
    if (underline) window_fill_rect(win, *x, (uint32_t)(*y + 8), 8, 1, color);
    *x += 8;
    return c == ' ' ? 0 : 1;
}

static uint16_t render_page_text(browser_t *browser, const net_info_t *info) {
    window_t *win = browser->win;
    uint16_t index = body_start(info);
    uint32_t text = browser_page_text_color(info, graphics_rgb(32, 36, 40));
    uint32_t heading = graphics_rgb(24, 76, 128);
    uint32_t link = graphics_rgb(20, 88, 190);
    uint32_t color_stack[8];
    uint8_t stack_depth = 0;
    uint32_t inline_color = text;
    uint32_t x = 10;
    uint32_t y = 34;
    uint8_t pending_space = 0;
    uint8_t in_heading = 0;
    uint8_t in_link = 0;
    uint8_t current_link = 255;
    uint16_t visible = 0;
    browser->link_count = 0;

    while (index < info->http_response_len && y < 112) {
        char c = (char)info->http_response[index++];
        if (c == '<') {
            uint8_t closing = 0;
            uint8_t is_br = 0;
            uint8_t is_p = 0;
            uint8_t is_div = 0;
            uint8_t is_heading = 0;
            uint8_t is_link = 0;
            uint8_t is_void = 0;
            uint16_t tag_start = index;
            uint16_t tag_end = 0;
            uint32_t styled_color = 0;
            char href[64];
            href[0] = 0;
            if (index < info->http_response_len && info->http_response[index] == '/') {
                closing = 1;
                index++;
                tag_start = index;
            }
            if (!closing && match_tag_name(info->http_response, index,
                                           info->http_response_len, "style")) {
                index = skip_element(info->http_response, index,
                                     info->http_response_len, "style");
                continue;
            }
            if (!closing && match_tag_name(info->http_response, index,
                                           info->http_response_len, "script")) {
                index = skip_element(info->http_response, index,
                                     info->http_response_len, "script");
                continue;
            }
            is_br = match_tag_name(info->http_response, index, info->http_response_len, "br");
            is_p = match_tag_name(info->http_response, index, info->http_response_len, "p");
            is_div = match_tag_name(info->http_response, index, info->http_response_len, "div");
            is_heading = match_tag_name(info->http_response, index, info->http_response_len, "h1") ||
                         match_tag_name(info->http_response, index, info->http_response_len, "h2");
            is_link = match_tag_name(info->http_response, index, info->http_response_len, "a");
            is_void = is_br ||
                      match_tag_name(info->http_response, index, info->http_response_len, "hr") ||
                      match_tag_name(info->http_response, index, info->http_response_len, "img") ||
                      match_tag_name(info->http_response, index, info->http_response_len, "meta") ||
                      match_tag_name(info->http_response, index, info->http_response_len, "link") ||
                      match_tag_name(info->http_response, index, info->http_response_len, "input");
            tag_end = find_tag_end(info->http_response, index, info->http_response_len);
            if (!closing && is_link &&
                tag_href_value(info->http_response, tag_start, tag_end,
                               href, sizeof(href))) {
                if (browser->link_count < BROWSER_MAX_LINKS) {
                    current_link = browser->link_count++;
                    browser->link_x[current_link] = 0;
                    browser->link_y[current_link] = 0;
                    browser->link_w[current_link] = 0;
                    browser->link_h[current_link] = 0;
                    browser_resolve_href(browser, href, browser->link_url[current_link],
                                         sizeof(browser->link_url[current_link]));
                } else {
                    current_link = 255;
                }
            }
            if (!closing && !is_void) {
                if (stack_depth < 8) color_stack[stack_depth++] = inline_color;
                if (tag_style_property(info->http_response, tag_start, tag_end,
                                       "color", &styled_color)) {
                    inline_color = styled_color;
                }
            }
            index = tag_end < info->http_response_len ? (uint16_t)(tag_end + 1) : tag_end;

            if (is_heading) {
                if (closing) {
                    in_heading = 0;
                    browser_newline(&x, &y, 10, &pending_space, 1);
                } else {
                    browser_newline(&x, &y, 10, &pending_space, 0);
                    in_heading = 1;
                }
            } else if (is_link) {
                in_link = closing ? 0 : 1;
                if (closing) current_link = 255;
            } else if (is_br || is_p || is_div) {
                browser_newline(&x, &y, 10, &pending_space, closing ? 1 : 0);
            }
            if (closing && stack_depth) inline_color = color_stack[--stack_depth];
            continue;
        }
        uint32_t color = in_link ? link : (in_heading ? heading : inline_color);
        if (c == '&') {
            if (pending_space && x > 10 && x < 216) {
                window_draw_char(win, x, y, ' ', color);
                if (in_link) window_fill_rect(win, x, (uint32_t)(y + 8), 8, 1, color);
                if (in_link) browser_track_link(browser, current_link, x, y);
                x += 8;
            }
            pending_space = 0;
            index = emit_entity(win, info->http_response, index,
                                info->http_response_len, &x, &y, color, in_link);
            if (in_link) browser_track_link(browser, current_link, (uint32_t)(x - 8), y);
            visible++;
        } else if (c == '\r' || c == '\n' || c == '\t' || c == ' ') {
            pending_space = 1;
        } else {
            if (c < 32 || c > 126) c = ' ';
            if (pending_space && x > 10 && x < 216) {
                window_draw_char(win, x, y, ' ', color);
                if (in_link) window_fill_rect(win, x, (uint32_t)(y + 8), 8, 1, color);
                if (in_link) browser_track_link(browser, current_link, x, y);
                x += 8;
            }
            pending_space = 0;
            if (x > 216) {
                x = 10;
                y += 10;
                if (y >= 112) break;
            }
            window_draw_char(win, x, y, c, color);
            if (in_link) window_fill_rect(win, x, (uint32_t)(y + 8), 8, 1, color);
            if (in_link) browser_track_link(browser, current_link, x, y);
            x += 8;
            if (c != ' ') visible++;
        }
    }
    return visible;
}

static uint16_t render_text_fallback(browser_t *browser, const net_info_t *info) {
    window_t *win = browser->win;
    uint16_t index = info->http_body_offset ? info->http_body_offset : 0;
    uint32_t x = 10;
    uint32_t y = 34;
    uint32_t color = graphics_rgb(32, 36, 40);
    uint8_t pending_space = 0;
    uint16_t visible = 0;

    browser->link_count = 0;
    while (index < info->http_response_len && y < 112) {
        char c = (char)info->http_response[index++];
        if (c == '<') {
            uint16_t tag_start = index;
            if (index < info->http_response_len && info->http_response[index] == '/') {
                tag_start = (uint16_t)(index + 1);
            }
            if (match_tag_name(info->http_response, tag_start,
                               info->http_response_len, "script")) {
                index = skip_element(info->http_response, tag_start,
                                     info->http_response_len, "script");
                pending_space = 1;
                continue;
            }
            if (match_tag_name(info->http_response, tag_start,
                               info->http_response_len, "style")) {
                index = skip_element(info->http_response, tag_start,
                                     info->http_response_len, "style");
                pending_space = 1;
                continue;
            }
            if (match_tag_name(info->http_response, tag_start,
                               info->http_response_len, "title") ||
                match_tag_name(info->http_response, tag_start,
                               info->http_response_len, "h1") ||
                match_tag_name(info->http_response, tag_start,
                               info->http_response_len, "h2") ||
                match_tag_name(info->http_response, tag_start,
                               info->http_response_len, "p") ||
                match_tag_name(info->http_response, tag_start,
                               info->http_response_len, "div") ||
                match_tag_name(info->http_response, tag_start,
                               info->http_response_len, "br")) {
                if (x != 10) {
                    x = 10;
                    y += 10;
                }
                pending_space = 0;
            } else {
                pending_space = 1;
            }
            index = skip_tag(info->http_response, (uint16_t)(index - 1),
                             info->http_response_len);
            continue;
        }
        if (c == '&') {
            c = ' ';
            if (match_word(info->http_response, index, info->http_response_len, "amp;")) c = '&';
            else if (match_word(info->http_response, index, info->http_response_len, "lt;")) c = '<';
            else if (match_word(info->http_response, index, info->http_response_len, "gt;")) c = '>';
            else if (match_word(info->http_response, index, info->http_response_len, "quot;")) c = '"';
            while (index < info->http_response_len && info->http_response[index] != ';') index++;
            if (index < info->http_response_len) index++;
        }
        if (c == '\r' || c == '\n' || c == '\t' || c == ' ') {
            pending_space = 1;
            continue;
        }
        if (pending_space && x > 10) {
            browser_draw_text_char(win, &x, &y, ' ', color, 0);
        }
        pending_space = 0;
        visible = (uint16_t)(visible + browser_draw_text_char(win, &x, &y, c, color, 0));
    }

    if (!visible) {
        char value[32];
        uint32_t muted = graphics_rgb(86, 92, 98);
        char title[48];
        char desc[64];
        char page_type[32];
        char lang[16];
        char host[48];
        char path[8];
        char summary[32];
        uint8_t drew_summary = 0;
        uint8_t content_seen = 0;
        uint32_t summary_y = 38;
        browser_current_host_path(browser, host, sizeof(host), path, sizeof(path));
        window_draw_string(win, 10, summary_y, "Host:", muted);
        browser_draw_string_limit_bg(win, 58, summary_y, host, 20, muted,
                                     browser_page_background(info, graphics_rgb(255, 255, 255)));
        drew_summary = 1;
        summary_y += 14;
        if (browser_title_text(info, title, sizeof(title))) {
            window_draw_string(win, 10, summary_y, "Title:", muted);
            browser_draw_string_limit_bg(win, 66, summary_y, title, 18, muted,
                                         browser_page_background(info, graphics_rgb(255, 255, 255)));
            summary_y += 14;
        }
        if (browser_meta_description(info, desc, sizeof(desc))) {
            window_draw_string(win, 10, summary_y, "Desc:", muted);
            browser_draw_string_limit_bg(win, 58, summary_y, desc, 20, muted,
                                         browser_page_background(info, graphics_rgb(255, 255, 255)));
            summary_y += 14;
        }
        if (browser_html_attr(info, "itemtype", page_type, sizeof(page_type))) {
            browser_short_type(page_type);
            window_draw_string(win, 10, summary_y, "Page:", muted);
            browser_draw_string_limit_bg(win, 58, summary_y, page_type, 20, muted,
                                         browser_page_background(info, graphics_rgb(255, 255, 255)));
            summary_y += 14;
        }
        if (browser_html_attr(info, "lang", lang, sizeof(lang))) {
            window_draw_string(win, 10, summary_y, "Lang:", muted);
            browser_draw_string_limit_bg(win, 58, summary_y, lang, 10, muted,
                                         browser_page_background(info, graphics_rgb(255, 255, 255)));
            summary_y += 14;
        }
        if (browser_meta_http_equiv(info, value, sizeof(value))) {
            browser_content_summary(value, summary, sizeof(summary));
            window_draw_string(win, 10, summary_y, "Doc:", muted);
            browser_draw_string_limit_bg(win, 50, summary_y, summary[0] ? summary : value, 20, muted,
                                         browser_page_background(info, graphics_rgb(255, 255, 255)));
            content_seen = 1;
            summary_y += 14;
        }
        if (!drew_summary) {
            window_draw_string(win, 10, 38, "No readable text", muted);
            summary_y = 54;
        }
        if (!content_seen && browser_header_value(info, "content-type", value, sizeof(value))) {
            browser_content_summary(value, summary, sizeof(summary));
            window_draw_string(win, 10, summary_y, "Type:", muted);
            browser_draw_string_limit_bg(win, 50, summary_y, summary[0] ? summary : value, 20, muted,
                                         browser_page_background(info, graphics_rgb(255, 255, 255)));
            summary_y += 14;
        }
        if (browser_header_value(info, "content-encoding", value, sizeof(value))) {
            window_draw_string(win, 10, summary_y, "Enc:", muted);
            browser_draw_string_limit_bg(win, 42, summary_y, value, 12, muted,
                                         browser_page_background(info, graphics_rgb(255, 255, 255)));
            summary_y += 14;
        }
        if (browser_header_value(info, "location", value, sizeof(value))) {
            window_draw_string(win, 10, summary_y, "Loc:", muted);
            browser_draw_string_limit_bg(win, 42, summary_y, value, 20, muted,
                                         browser_page_background(info, graphics_rgb(255, 255, 255)));
        }
    }
    return visible;
}

static void render_source_view(browser_t *browser, const net_info_t *info) {
    window_t *win = browser->win;
    uint16_t body = info->http_body_offset ? info->http_body_offset : 0;
    uint16_t available = info->http_response_len > body ?
                         (uint16_t)(info->http_response_len - body) : 0;
    if (browser->source_offset > available) browser->source_offset = available;
    uint16_t index = (uint16_t)(body + browser->source_offset);
    uint32_t x = 10;
    uint32_t y = 34;
    uint32_t color = graphics_rgb(32, 36, 40);
    uint16_t chars = 0;

    browser->link_count = 0;
    while (index < info->http_response_len && y < 112 && chars < 220) {
        char c = (char)info->http_response[index++];
        if (c == '\r') continue;
        if (c == '\n' || c == '\t') c = ' ';
        if (c < 32 || c > 126) c = '.';
        browser_draw_text_char(win, &x, &y, c, color, 0);
        chars++;
    }
    if (!chars) {
        window_draw_string(win, 10, 42, "No source bytes", graphics_rgb(86, 92, 98));
    }
    if (browser->source_searching) {
        window_draw_string(win, 10, 116, "/", graphics_rgb(86, 92, 98));
        browser_draw_string_limit_bg(win, 18, 116, browser->source_query, 16,
                                     graphics_rgb(86, 92, 98),
                                     browser_page_background(info, graphics_rgb(255, 255, 255)));
    } else {
        window_draw_string(win, 10, 116, "Off", graphics_rgb(86, 92, 98));
        draw_num(win, 42, 116, browser->source_offset, graphics_rgb(86, 92, 98));
    }
}

static int browser_source_find(browser_t *browser,
                               const net_info_t *info,
                               const char *needle,
                               const char *status) {
    if (!browser || !info || !needle) return 0;
    uint16_t body = info->http_body_offset ? info->http_body_offset : 0;
    if (body >= info->http_response_len) {
        browser->status = "MISS";
        return 0;
    }

    uint16_t first = (uint16_t)(body + browser->source_offset + 1);
    if (first >= info->http_response_len) first = body;
    for (uint8_t pass = 0; pass < 2; pass++) {
        uint16_t start = pass ? body : first;
        uint16_t end = pass ? first : info->http_response_len;
        for (uint16_t i = start; i < end; i++) {
            if (!match_word(info->http_response, i, info->http_response_len, needle)) {
                continue;
            }
            browser->source_offset = (uint16_t)(i - body);
            browser->status = status;
            return 1;
        }
    }
    browser->status = "MISS";
    return 0;
}

static int browser_back(browser_t *browser) {
    if (!browser || !browser->has_history || !browser->history_url[0]) {
        if (browser) browser->status = "NOH";
        return 0;
    }
    char current[64];
    browser_copy(current, sizeof(current), browser->url);
    browser_set_url(browser, browser->history_url);
    browser_copy(browser->history_url, sizeof(browser->history_url), current);
    browser->editing_url = 0;
    browser->source_view = 0;
    browser_clear_source_search(browser);
    browser_refresh(browser);
    browser->status = "BACK";
    return 1;
}

static int browser_reload(browser_t *browser) {
    if (!browser || !browser->win) return 0;
    browser->editing_url = 0;
    browser->source_view = 0;
    browser_clear_source_search(browser);
    int ok = browser_refresh(browser);
    browser->status = ok ? "RLD" : "WAIT";
    return ok;
}

static int browser_home(browser_t *browser) {
    if (!browser || !browser->win) return 0;
    browser_open_url(window_get_x(browser->win), window_get_y(browser->win),
                     BROWSER_HOME_URL);
    return 1;
}

browser_t *browser_open_url(uint32_t x, uint32_t y, const char *url) {
    browser_t *browser = &g_browser;
    if (!browser->win) {
        browser->win = window_create(x, y, 236, 142, "Browser");
        if (!browser->win) return 0;
        window_set_bg_color(browser->win, graphics_rgb(236, 238, 240));
        window_set_title_color(browser->win,
                               graphics_rgb(36, 84, 120),
                               graphics_rgb(255, 255, 255));
        browser->status = "READY";
        browser->editing_url = 0;
        browser->source_view = 0;
        browser_clear_source_search(browser);
        browser_reset_source_scroll(browser);
        window_manager_add(browser->win);
    }

    if (browser->win && browser->url[0] && url && url[0] &&
        !text_equals_ci(browser->url, url)) {
        browser_remember_current(browser);
    }
    browser_set_url(browser, url);
    browser->editing_url = 0;
    browser->source_view = 0;
    browser_clear_source_search(browser);
    browser_reset_source_scroll(browser);
    browser_refresh(browser);
    if (window_is_minimized(browser->win)) window_toggle_minimize(browser->win);
    window_set_focus(browser->win);
    return browser;
}

browser_t *browser_open(uint32_t x, uint32_t y) {
    return browser_open_url(x, y, BROWSER_HOME_URL);
}

browser_t *browser_active(void) {
    return g_browser.win ? &g_browser : 0;
}

int browser_refresh(browser_t *browser) {
    if (!browser) browser = &g_browser;
    browser_clear_source_search(browser);
    browser_reset_source_scroll(browser);
    int ok = 0;
    uint8_t redirects = 0;
    for (uint8_t attempt = 0; attempt < 3; attempt++) {
        const char *url = browser->url[0] ? browser->url : "example.com/";
        ok = browser_is_https(url) ? net_https_get_url(url) : net_http_get_url(url);
        if (!ok) continue;

        char redirect[64];
        if (redirects < 2 &&
            browser_redirect_location(browser, net_info(), redirect, sizeof(redirect))) {
            browser_set_url(browser, redirect);
            browser->status = "REDIR";
            redirects++;
            ok = 0;
            continue;
        }
        break;
    }
    browser->status = ok ? (browser_is_https(browser->url) ? "TLS" : "FETCHED") : "WAIT";
    return ok;
}

static void browser_draw_net_diag(window_t *win, const net_info_t *info, uint32_t color) {
    window_draw_string(win, 58, 52, "No page", color);
    window_draw_string(win, 20, 72, "S:", color);
    draw_num(win, 36, 72, info->http_stage, color);
    window_draw_string(win, 68, 72, "T:", color);
    draw_num(win, 84, 72, info->tcp_payload_tx, color);
    window_draw_string(win, 124, 72, "A:", color);
    draw_num(win, 140, 72, info->tcp_payload_acked, color);
    window_draw_string(win, 20, 86, "R:", color);
    draw_num(win, 36, 86, info->tcp_payload_rx, color);
    window_draw_string(win, 76, 86, "Err:", color);
    draw_num(win, 108, 86, info->tcp_errors, color);
    window_draw_string(win, 20, 100, "TLS:", color);
    draw_num(win, 52, 100, info->tls_stage, color);
    window_draw_string(win, 84, 100, "D:", color);
    draw_num(win, 100, 100, info->tls_app_decrypt, color);
    window_draw_string(win, 132, 100, "P:", color);
    draw_num(win, 148, 100, info->tls_app_pending_rx, color);
}

void browser_render(browser_t *browser) {
    if (!browser || !browser->win || window_is_minimized(browser->win)) return;

    const net_info_t *info = net_info();
    uint32_t bg = graphics_rgb(236, 238, 240);
    uint32_t panel = graphics_rgb(255, 255, 255);
    uint32_t page = browser_page_background(info, panel);
    uint32_t chrome = graphics_rgb(210, 218, 224);
    uint32_t muted = graphics_rgb(86, 92, 98);
    uint32_t accent = info->http_valid ? graphics_rgb(45, 135, 82)
                                       : graphics_rgb(175, 80, 70);

    window_clear(browser->win, bg);
    window_fill_rect(browser->win, 8, 8, 218, 18, chrome);
    window_fill_rect(browser->win, 12, 12, 158, 10, panel);
    browser_draw_string_limit_bg(browser->win, 16, 13,
                                 browser_display_url(browser),
                                 18, muted, panel);
    if (browser->editing_url) {
        uint16_t len = browser_url_len(browser);
        uint32_t cx = 16 + (uint32_t)(len < 18 ? len : 18) * 8;
        if (cx > 156) cx = 156;
        window_fill_rect(browser->win, cx, 21, 7, 1, muted);
    }
    window_fill_rect(browser->win, 176, 12, 42, 10, accent);
    browser_draw_string_limit_bg(browser->win, 181, 13,
                                 browser_status_label(browser->status),
                                 4, panel, accent);

    window_fill_rect(browser->win, 8, 30, 218, 96, page);
    if (!info->http_response_len) {
        browser_draw_net_diag(browser->win, info, muted);
    } else if (browser->source_view) {
        render_source_view(browser, info);
    } else {
        if (!render_page_text(browser, info)) {
            render_text_fallback(browser, info);
        }
    }
    window_draw_string(browser->win, 10, 128,
                       browser_is_https(browser->url) ? "HTTPS" : "HTTP ",
                       muted);
    draw_num(browser->win, 50, 128, info->http_status, muted);
    window_draw_string(browser->win, 86, 128, " bytes ", muted);
    draw_num(browser->win, 142, 128, info->http_response_len, muted);
}

int browser_handle_click(browser_t *browser, int32_t x, int32_t y) {
    if (!browser || !browser->win) {
        return 0;
    }
    if (x >= 176 && x < 218 && y >= 8 && y < 26) {
        browser_reload(browser);
        return 1;
    }
    if (x >= 12 && x < 170 && y >= 8 && y < 26) {
        browser_remember_current(browser);
        browser->editing_url = 1;
        browser_clear_source_search(browser);
        browser->status = "EDIT";
        return 1;
    }
    browser->editing_url = 0;
    browser_clear_source_search(browser);
    for (uint8_t i = 0; i < browser->link_count; i++) {
        if (!browser->link_w[i] || !browser->link_url[i][0]) continue;
        if (x < (int32_t)browser->link_x[i] ||
            y < (int32_t)browser->link_y[i] ||
            x >= (int32_t)(browser->link_x[i] + browser->link_w[i]) ||
            y >= (int32_t)(browser->link_y[i] + browser->link_h[i])) {
            continue;
        }
        browser_open_url(window_get_x(browser->win), window_get_y(browser->win),
                         browser->link_url[i]);
        return 1;
    }
    return 0;
}

void browser_handle_key(browser_t *browser, char c) {
    if (!browser || !browser->win) return;
    if (!browser->editing_url && browser->source_view && browser->source_searching) {
        if (c == '\n') {
            if (browser->source_query[0]) {
                browser_source_find(browser, net_info(), browser->source_query, "FIND");
            } else {
                browser->status = "SRC";
            }
            browser->source_searching = 0;
            return;
        }
        if (c == 27) {
            browser_clear_source_search(browser);
            browser->status = "SRC";
            return;
        }
        if (c == '\b') {
            uint16_t len = browser_source_query_len(browser);
            if (len) browser->source_query[len - 1] = 0;
            return;
        }
        if (c < 32 || c > 126) return;
        uint16_t len = browser_source_query_len(browser);
        if ((uint32_t)(len + 1) < sizeof(browser->source_query)) {
            browser->source_query[len] = lower_char(c);
            browser->source_query[len + 1] = 0;
        }
        return;
    }
    if (!browser->editing_url && (c == 'v' || c == 'V')) {
        browser->source_view = browser->source_view ? 0 : 1;
        browser->status = browser->source_view ? "SRC" : "READY";
        browser_clear_source_search(browser);
        return;
    }
    if (!browser->editing_url && !browser->source_view &&
        (c == 'h' || c == 'H' || c == '\b')) {
        browser_back(browser);
        return;
    }
    if (!browser->editing_url && !browser->source_view &&
        (c == 'r' || c == 'R')) {
        browser_reload(browser);
        return;
    }
    if (!browser->editing_url && !browser->source_view &&
        (c == 'o' || c == 'O')) {
        browser_home(browser);
        return;
    }
    if (!browser->editing_url && browser->source_view) {
        uint16_t step = 180;
        if (c == '/') {
            browser->source_searching = 1;
            browser->source_query[0] = 0;
            browser->status = "FIND";
            return;
        }
        if (c == 'j' || c == 'J' || c == 'n' || c == 'N' || c == ' ') {
            uint32_t next = (uint32_t)browser->source_offset + step;
            browser->source_offset = next > 65535u ? 65535u : (uint16_t)next;
            browser->status = "SRC";
            return;
        }
        if (c == 'k' || c == 'K' || c == 'p' || c == 'P' || c == '\b') {
            browser->source_offset = browser->source_offset > step ?
                                     (uint16_t)(browser->source_offset - step) : 0;
            browser->status = "SRC";
            return;
        }
        if (c == 'g' || c == 'G') {
            browser_reset_source_scroll(browser);
            browser->status = "SRC";
            return;
        }
        if (c == 't' || c == 'T') {
            browser_source_find(browser, net_info(), "<title", "TITL");
            return;
        }
        if (c == 'm' || c == 'M') {
            browser_source_find(browser, net_info(), "<meta", "META");
            return;
        }
        if (c == 'b' || c == 'B') {
            browser_source_find(browser, net_info(), "<body", "BODY");
            return;
        }
        if (c == 'l' || c == 'L') {
            browser_source_find(browser, net_info(), "href", "LINK");
            return;
        }
        if (c == 's' || c == 'S') {
            browser_source_find(browser, net_info(), "<script", "SCR");
            return;
        }
    }
    if (!browser->editing_url) return;

    if (c == '\n') {
        browser->editing_url = 0;
        browser->source_view = 0;
        browser_reset_source_scroll(browser);
        browser_refresh(browser);
        return;
    }
    if (c == 27) {
        browser->editing_url = 0;
        browser->status = "READY";
        return;
    }
    if (c == '\b') {
        uint16_t len = browser_url_len(browser);
        if (len) browser->url[len - 1] = 0;
        return;
    }
    if (c < 32 || c > 126) return;

    uint16_t len = browser_url_len(browser);
    if ((uint32_t)(len + 1) < sizeof(browser->url)) {
        browser->url[len] = c;
        browser->url[len + 1] = 0;
    }
}

void browser_window_closed(browser_t *browser, window_t *win) {
    if (browser && browser->win == win) {
        browser->win = 0;
    }
}
