#include <stdint.h>
#include "term_window.h"
#include "window.h"
#include "graphics.h"
#include "libc.h"
#include "serial.h"
#include "pmm.h"
#include "io.h"
#include "fs.h"
#include "text_editor.h"
#include "image_viewer.h"
#include "audio_player.h"
#include "browser.h"
#include "scheduler.h"
#include "process.h"
#include "gdt.h"
#include "interrupts.h"
#include "usermode.h"
#include "user_scheduler.h"
#include "e1000.h"
#include "net.h"
#include "rtc.h"
#include "p256.h"
#include "theme.h"

static term_window_t g_tw;
static uint8_t file_buffer[512];
static uint8_t app_write_buffer[512];

static void tw_scroll(term_window_t *t) {
    for (int r = 0; r < TWIN_ROWS - 1; r++)
        memcpy(t->buf[r], t->buf[r + 1], TWIN_COLS);
    memset(t->buf[TWIN_ROWS - 1], 0, TWIN_COLS);
    t->cur_row = TWIN_ROWS - 1;
    t->cur_col = 0;
}

void term_window_putc(term_window_t *t, char c) {
    if (c == '\n') {
        t->cur_col = 0;
        t->cur_row++;
        if (t->cur_row >= TWIN_ROWS) tw_scroll(t);
    } else if (c == '\b') {
        if (t->cur_col > 0) {
            t->cur_col--;
            t->buf[t->cur_row][t->cur_col] = 0;
        }
    } else {
        if (t->cur_col >= TWIN_COLS) {
            t->cur_col = 0;
            t->cur_row++;
            if (t->cur_row >= TWIN_ROWS) tw_scroll(t);
        }
        t->buf[t->cur_row][t->cur_col++] = c;
    }
}

void term_window_print(term_window_t *t, const char *s) {
    while (*s) term_window_putc(t, *s++);
}

static void tw_print_num(term_window_t *t, uint32_t n) {
    char tmp[12];
    int i = 11;
    tmp[i] = 0;
    if (n == 0) { term_window_putc(t, '0'); return; }
    while (n > 0) { tmp[--i] = '0' + (char)(n % 10); n /= 10; }
    term_window_print(t, tmp + i);
}

static void tw_print_hex32(term_window_t *t, uint32_t n) {
    const char *digits = "0123456789ABCDEF";
    for (int shift = 28; shift >= 0; shift -= 4) {
        term_window_putc(t, digits[(n >> shift) & 0xFu]);
    }
}

static void tw_print_hex8(term_window_t *t, uint8_t n) {
    const char *digits = "0123456789ABCDEF";
    term_window_putc(t, digits[(n >> 4) & 0xFu]);
    term_window_putc(t, digits[n & 0xFu]);
}

static void tw_print_ip(term_window_t *t, const uint8_t ip[4]) {
    for (uint32_t i = 0; i < 4; i++) {
        if (i) term_window_putc(t, '.');
        tw_print_num(t, ip[i]);
    }
}

static void tw_print_mac(term_window_t *t, const uint8_t mac[6]) {
    for (uint32_t i = 0; i < 6; i++) {
        if (i) term_window_putc(t, ':');
        tw_print_hex8(t, mac[i]);
    }
}

static void tw_print_theme_pair(term_window_t *t, const char *name, theme_color_t color) {
    term_window_print(t, name);
    term_window_putc(t, ':');
    tw_print_num(t, theme_color(color));
    term_window_putc(t, ' ');
}

static void tw_print_theme(term_window_t *t) {
    term_window_print(t, "Theme ");
    term_window_print(t, theme_name());
    term_window_putc(t, '\n');
    tw_print_theme_pair(t, "wall", THEME_WALLPAPER);
    tw_print_theme_pair(t, "bar", THEME_TASKBAR);
    tw_print_theme_pair(t, "win", THEME_WINDOW_BG);
    term_window_putc(t, '\n');
    tw_print_theme_pair(t, "title", THEME_TITLE_BG);
    tw_print_theme_pair(t, "focus", THEME_BORDER_FOCUS);
    tw_print_theme_pair(t, "idle", THEME_BORDER_IDLE);
    term_window_putc(t, '\n');
    tw_print_theme_pair(t, "close", THEME_CLOSE_BG);
    tw_print_theme_pair(t, "min", THEME_MIN_BG);
    tw_print_theme_pair(t, "icon", THEME_ICON_TEXT);
    term_window_putc(t, '\n');
}

static void tw_print_theme_list(term_window_t *t) {
    term_window_print(t, "Themes:");
    for (uint8_t i = 0; i < theme_count(); i++) {
        term_window_putc(t, ' ');
        term_window_print(t, theme_name_at(i));
    }
    term_window_putc(t, '\n');
}

static char tw_lower(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static int tw_match_word(const uint8_t *data, uint16_t index, uint16_t len, const char *word) {
    while (*word && index < len) {
        if (tw_lower((char)data[index++]) != *word++) return 0;
    }
    return *word == 0;
}

static uint16_t tw_skip_tag(const uint8_t *data, uint16_t index, uint16_t len) {
    while (index < len && data[index] != '>') index++;
    return index < len ? (uint16_t)(index + 1) : index;
}

static uint16_t tw_skip_element(const uint8_t *data,
                                uint16_t index,
                                uint16_t len,
                                const char *name) {
    index = tw_skip_tag(data, index, len);
    while (index + 3 < len) {
        if (data[index] == '<' &&
            index + 2 < len &&
            data[index + 1] == '/' &&
            tw_match_word(data, (uint16_t)(index + 2), len, name)) {
            return tw_skip_tag(data, index, len);
        }
        index++;
    }
    return index;
}

static uint16_t tw_find_body_start(const net_info_t *info) {
    uint16_t start = info->http_body_offset ? info->http_body_offset : 0;
    for (uint16_t i = start; i + 5 < info->http_response_len; i++) {
        if (info->http_response[i] == '<' &&
            tw_match_word(info->http_response, (uint16_t)(i + 1),
                          info->http_response_len, "body")) {
            while (i < info->http_response_len && info->http_response[i] != '>') i++;
            if (i < info->http_response_len) return (uint16_t)(i + 1);
        }
    }
    return start;
}

static uint16_t tw_skip_html_entity(term_window_t *t,
                                    const uint8_t *data,
                                    uint16_t index,
                                    uint16_t len,
                                    uint8_t *cols) {
    char out = ' ';
    if (tw_match_word(data, index, len, "amp;")) out = '&';
    else if (tw_match_word(data, index, len, "lt;")) out = '<';
    else if (tw_match_word(data, index, len, "gt;")) out = '>';
    else if (tw_match_word(data, index, len, "quot;")) out = '"';
    else if (tw_match_word(data, index, len, "nbsp;")) out = ' ';

    if (*cols < TWIN_COLS) {
        term_window_putc(t, out);
        (*cols)++;
    }
    while (index < len && data[index] != ';') index++;
    return index < len ? (uint16_t)(index + 1) : index;
}

static void tw_print_http_page(term_window_t *t, const net_info_t *info) {
    uint16_t index = tw_find_body_start(info);
    uint8_t rows = 0;
    uint8_t cols = 0;
    uint8_t pending_space = 0;

    term_window_print(t, "Page ");
    tw_print_num(t, info->http_status);
    term_window_putc(t, ' ');
    tw_print_num(t, info->http_response_len);
    term_window_print(t, "b\n");

    while (index < info->http_response_len && rows < 8) {
        char c = (char)info->http_response[index++];
        if (c == '<') {
            uint8_t block = 0;
            uint8_t closing = 0;
            if (index < info->http_response_len && info->http_response[index] == '/') {
                closing = 1;
                index++;
            }
            if (!closing && tw_match_word(info->http_response, index,
                                          info->http_response_len, "style")) {
                index = tw_skip_element(info->http_response, index,
                                        info->http_response_len, "style");
                continue;
            }
            if (!closing && tw_match_word(info->http_response, index,
                                          info->http_response_len, "script")) {
                index = tw_skip_element(info->http_response, index,
                                        info->http_response_len, "script");
                continue;
            }
            if (tw_match_word(info->http_response, index, info->http_response_len, "br") ||
                tw_match_word(info->http_response, index, info->http_response_len, "p") ||
                tw_match_word(info->http_response, index, info->http_response_len, "h1") ||
                tw_match_word(info->http_response, index, info->http_response_len, "h2") ||
                tw_match_word(info->http_response, index, info->http_response_len, "div")) {
                block = 1;
            }
            index = tw_skip_tag(info->http_response, index, info->http_response_len);
            if (block && cols > 0) {
                term_window_putc(t, '\n');
                rows++;
                cols = 0;
                pending_space = 0;
            }
            continue;
        }
        if (c == '&') {
            if (pending_space && cols > 0 && cols < TWIN_COLS) {
                term_window_putc(t, ' ');
                cols++;
            }
            pending_space = 0;
            index = tw_skip_html_entity(t, info->http_response, index,
                                        info->http_response_len, &cols);
        } else if (c == '\r' || c == '\n' || c == '\t' || c == ' ') {
            pending_space = 1;
        } else {
            if (c < 32 || c > 126) c = ' ';
            if (pending_space && cols > 0 && cols < TWIN_COLS) {
                term_window_putc(t, ' ');
                cols++;
            }
            pending_space = 0;
            if (cols >= TWIN_COLS) {
                term_window_putc(t, '\n');
                rows++;
                cols = 0;
                if (rows >= 8) break;
            }
            term_window_putc(t, c);
            cols++;
        }
        if (cols >= TWIN_COLS) {
            term_window_putc(t, '\n');
            rows++;
            cols = 0;
            pending_space = 0;
        }
    }
    if (cols > 0 && rows < 8) term_window_putc(t, '\n');
}

static void tw_print_http_diag(term_window_t *t) {
    const net_info_t *info = net_info();
    term_window_print(t, "S:");
    tw_print_num(t, info->http_stage);
    term_window_print(t, " P:");
    tw_print_num(t, info->tcp_payload_send_errors);
    term_window_print(t, " T:");
    tw_print_num(t, info->tcp_payload_tx);
    term_window_print(t, " A:");
    tw_print_num(t, info->tcp_payload_acked);
    term_window_print(t, " R:");
    tw_print_num(t, info->tcp_payload_rx);
    term_window_putc(t, '\n');
    term_window_print(t, "Code:");
    tw_print_num(t, info->http_status);
    term_window_putc(t, ' ');
    term_window_print(t, "Len:");
    tw_print_num(t, info->http_request_len);
    term_window_putc(t, '\n');
    term_window_print(t, "Host:");
    term_window_print(t, info->http_host);
    term_window_putc(t, '\n');
    term_window_print(t, "Path:");
    term_window_print(t, info->http_path);
    term_window_putc(t, '\n');
}

static void tw_print_tls_diag(term_window_t *t) {
    const net_info_t *info = net_info();
    term_window_print(t, "TLS stage:");
    tw_print_num(t, info->tls_stage);
    term_window_print(t, " H:");
    tw_print_num(t, info->tls_client_hello_len);
    term_window_putc(t, '\n');
    term_window_print(t, "Host:");
    term_window_print(t, info->http_host);
    term_window_putc(t, '\n');
    term_window_print(t, "Tgt:");
    tw_print_ip(t, info->tcp_target_ip);
    term_window_putc(t, ':');
    tw_print_num(t, info->tcp_target_port);
    term_window_putc(t, '\n');
    term_window_print(t, "Tx:");
    tw_print_num(t, info->tcp_payload_tx);
    term_window_print(t, " A:");
    tw_print_num(t, info->tcp_payload_acked);
    term_window_print(t, " R:");
    tw_print_num(t, info->tcp_payload_rx);
    term_window_putc(t, '\n');
    term_window_print(t, "Rec:");
    tw_print_hex8(t, info->tls_record_type);
    term_window_print(t, " Ver:");
    tw_print_hex32(t, info->tls_record_version);
    term_window_putc(t, '\n');
    term_window_print(t, "Len:");
    tw_print_num(t, info->tls_record_len);
    term_window_print(t, " Hs:");
    tw_print_hex8(t, info->tls_handshake_type);
    term_window_putc(t, '\n');
    term_window_print(t, "ServerHello:");
    tw_print_num(t, info->tls_server_hello);
    term_window_putc(t, '\n');
    term_window_print(t, "SV:");
    tw_print_hex32(t, info->tls_server_version);
    term_window_print(t, " CS:");
    tw_print_hex32(t, info->tls_cipher_suite);
    term_window_putc(t, '\n');
    term_window_print(t, "Cert:");
    tw_print_num(t, info->tls_certificate);
    term_window_print(t, " CL:");
    tw_print_num(t, info->tls_certificate_len);
    term_window_putc(t, '\n');
    term_window_print(t, "CRx:");
    tw_print_num(t, info->tls_certificate_rx);
    term_window_print(t, " Rem:");
    tw_print_num(t, info->tls_record_pending);
    term_window_putc(t, '\n');
    term_window_print(t, "Kex:");
    tw_print_num(t, info->tls_server_key_exchange);
    term_window_print(t, " KL:");
    tw_print_num(t, info->tls_server_key_exchange_len);
    term_window_putc(t, '\n');
    term_window_print(t, "Run net cert/kex\n");
}

static uint8_t tw_cert_date_ok(const net_info_t *info, rtc_time_t *now) {
    now->yyyymmdd = 0;
    if (!rtc_read_time(now)) return 2;
    if (!info->tls_x509_not_before_date ||
        !info->tls_x509_not_after_date) {
        return 2;
    }
    return (now->yyyymmdd >= info->tls_x509_not_before_date &&
            now->yyyymmdd <= info->tls_x509_not_after_date) ? 1 : 0;
}

static void tw_print_check(term_window_t *t, uint8_t value) {
    if (value == 2) {
        term_window_putc(t, '?');
    } else {
        tw_print_num(t, value);
    }
}

static void tw_print_trust_diag(term_window_t *t) {
    const net_info_t *info = net_info();
    rtc_time_t now;
    uint8_t date_ok;
    uint8_t leaf_ok;
    uint8_t ku_ok;
    uint8_t eku_ok;
    uint8_t issuer_ok;
    uint8_t chain_ok;
    uint8_t partial_ok;
    uint8_t sig_ok;
    uint8_t trust_ok;

    if (!info->tls_certificate) {
        term_window_print(t, "No cert\n");
        term_window_print(t, "Run net tls\n");
        return;
    }

    date_ok = tw_cert_date_ok(info, &now);
    leaf_ok = (info->tls_x509_basic_constraints &&
               !info->tls_x509_is_ca) ? 1 : 0;
    ku_ok = info->tls_x509_key_usage ?
            ((info->tls_x509_key_usage_bits & 3u) ? 1 : 0) : 2;
    eku_ok = info->tls_x509_eku ?
             ((info->tls_x509_eku_bits & 1u) ? 1 : 0) : 2;
    issuer_ok = info->tls_x509_known_issuer;
    chain_ok = info->tls_chain_link;
    partial_ok = info->tls_x509_host_match &&
                 date_ok == 1 &&
                 leaf_ok &&
                 issuer_ok &&
                 chain_ok &&
                 ku_ok == 1 &&
                 eku_ok == 1;
    if (!info->tls_x509_ecdsa_point_done) {
        net_tls_verify_signature();
    }
    sig_ok = (info->tls_x509_ecdsa_point_done &&
              info->tls_x509_ecdsa_match) ? 1 : 0;
    trust_ok = partial_ok && sig_ok;

    term_window_print(t, "Trust:");
    term_window_print(t, trust_ok ? "OK" : (partial_ok ? "PARTIAL" : "FAIL"));
    term_window_putc(t, '\n');
    term_window_print(t, "H:");
    tw_print_num(t, info->tls_x509_host_match);
    term_window_print(t, " D:");
    tw_print_check(t, date_ok);
    term_window_print(t, " L:");
    tw_print_num(t, leaf_ok);
    term_window_putc(t, '\n');
    term_window_print(t, "I:");
    tw_print_num(t, issuer_ok);
    term_window_print(t, " C:");
    tw_print_num(t, chain_ok);
    term_window_print(t, " S:");
    tw_print_num(t, sig_ok);
    term_window_putc(t, '\n');
    term_window_print(t, "KU:");
    tw_print_check(t, ku_ok);
    term_window_print(t, " EKU:");
    tw_print_check(t, eku_ok);
    term_window_putc(t, '\n');
    if (trust_ok) {
        term_window_print(t, "Ready:HTTPS\n");
    } else if (partial_ok) {
        term_window_print(t, "Need:SIG\n");
    } else if (!chain_ok) {
        term_window_print(t, "Need:CHAIN+SIG\n");
    } else {
        term_window_print(t, "Need:TRUST\n");
    }
}

static void tw_print_sig_diag(term_window_t *t) {
    const net_info_t *info = net_info();

    if (!info->tls_certificate) {
        term_window_print(t, "No cert\n");
        term_window_print(t, "Run net tls\n");
        return;
    }

    term_window_print(t, "Sig:");
    tw_print_num(t, info->tls_x509_sig_alg);
    term_window_print(t, " OS:");
    tw_print_num(t, info->tls_x509_outer_sig_alg);
    term_window_putc(t, '\n');
    term_window_print(t, "SVal:");
    tw_print_num(t, info->tls_x509_signature_len);
    term_window_print(t, " U:");
    tw_print_num(t, info->tls_x509_signature_unused_bits);
    term_window_putc(t, '\n');
    term_window_print(t, "R:");
    tw_print_hex32(t, info->tls_x509_signature_r32);
    term_window_print(t, " L:");
    tw_print_num(t, info->tls_x509_signature_r_len);
    term_window_putc(t, '\n');
    term_window_print(t, "S:");
    tw_print_hex32(t, info->tls_x509_signature_s32);
    term_window_print(t, " L:");
    tw_print_num(t, info->tls_x509_signature_s_len);
    term_window_putc(t, '\n');
    term_window_print(t, "TBS:");
    tw_print_num(t, info->tls_x509_tbs_len);
    term_window_print(t, " H:");
    tw_print_num(t, info->tls_x509_tbs_hash_alg);
    term_window_putc(t, '\n');
    term_window_print(t, "Hash:");
    tw_print_hex32(t, info->tls_x509_tbs_hash32);
    term_window_putc(t, '\n');
    term_window_print(t, "IK:");
    tw_print_num(t, info->tls_x509_chain_pubkey_alg);
    term_window_print(t, " CV:");
    tw_print_num(t, info->tls_x509_chain_curve);
    term_window_putc(t, '\n');
    term_window_print(t, "KLen:");
    tw_print_num(t, info->tls_x509_chain_pubkey_len);
    term_window_putc(t, '\n');
    term_window_print(t, "X:");
    tw_print_hex32(t, info->tls_x509_chain_pubkey_x32);
    term_window_putc(t, '\n');
    term_window_print(t, "Y:");
    tw_print_hex32(t, info->tls_x509_chain_pubkey_y32);
    term_window_putc(t, '\n');
    term_window_print(t, "Chain:");
    tw_print_num(t, info->tls_chain_link);
    term_window_print(t, " VIn:");
    tw_print_num(t, info->tls_x509_verify_inputs);
    term_window_putc(t, '\n');
    term_window_print(t, "Sc:");
    tw_print_num(t, info->tls_x509_ecdsa_scalar_inputs);
    term_window_print(t, " Q:");
    tw_print_num(t, info->tls_x509_ecdsa_pubkey_valid);
    term_window_putc(t, '\n');
    term_window_print(t, " W:");
    tw_print_hex32(t, info->tls_x509_ecdsa_w32);
    term_window_putc(t, '\n');
    term_window_print(t, "U1:");
    tw_print_hex32(t, info->tls_x509_ecdsa_u1_32);
    term_window_print(t, " U2:");
    tw_print_hex32(t, info->tls_x509_ecdsa_u2_32);
    term_window_putc(t, '\n');
    term_window_print(t, "P:");
    tw_print_num(t, info->tls_x509_ecdsa_point_done);
    term_window_print(t, " V:");
    tw_print_hex32(t, info->tls_x509_ecdsa_v32);
    term_window_putc(t, '\n');
    term_window_print(t, "SigOK:");
    tw_print_num(t, info->tls_x509_ecdsa_match);
    term_window_putc(t, '\n');
}

static void tw_print_kex_diag(term_window_t *t) {
    const net_info_t *info = net_info();

    if (!info->tls_server_key_exchange) {
        term_window_print(t, "No kex\n");
        term_window_print(t, "Run net tls\n");
        return;
    }
    if (!info->tls_kex_sig_point_done) {
        net_tls_verify_kex_signature();
    }
    info = net_info();

    term_window_print(t, "Kex:");
    tw_print_num(t, info->tls_server_key_exchange);
    term_window_print(t, " L:");
    tw_print_num(t, info->tls_server_key_exchange_len);
    term_window_putc(t, '\n');
    term_window_print(t, "CurveT:");
    tw_print_num(t, info->tls_kex_curve_type);
    term_window_print(t, " C:");
    tw_print_hex32(t, info->tls_kex_named_curve);
    term_window_putc(t, '\n');
    term_window_print(t, "KeyL:");
    tw_print_num(t, info->tls_kex_pubkey_len);
    term_window_print(t, " V:");
    tw_print_num(t, info->tls_kex_pubkey_valid);
    term_window_putc(t, '\n');
    term_window_print(t, "X:");
    tw_print_hex32(t, info->tls_kex_pubkey_x32);
    term_window_putc(t, '\n');
    term_window_print(t, "Y:");
    tw_print_hex32(t, info->tls_kex_pubkey_y32);
    term_window_putc(t, '\n');
    term_window_print(t, "Sig H:");
    tw_print_num(t, info->tls_kex_sig_hash);
    term_window_print(t, " A:");
    tw_print_num(t, info->tls_kex_sig_alg);
    term_window_putc(t, '\n');
    term_window_print(t, "SL:");
    tw_print_num(t, info->tls_kex_sig_len);
    term_window_print(t, " R:");
    tw_print_hex32(t, info->tls_kex_sig_r32);
    term_window_putc(t, '\n');
    term_window_print(t, "RL:");
    tw_print_num(t, info->tls_kex_sig_r_len);
    term_window_print(t, " S:");
    tw_print_hex32(t, info->tls_kex_sig_s32);
    term_window_putc(t, '\n');
    term_window_print(t, "SLen:");
    tw_print_num(t, info->tls_kex_sig_s_len);
    term_window_putc(t, '\n');
    term_window_print(t, "LeafK:");
    tw_print_num(t, info->tls_x509_pubkey_alg);
    term_window_print(t, " C:");
    tw_print_num(t, info->tls_x509_pubkey_curve);
    term_window_putc(t, '\n');
    term_window_print(t, "KH:");
    tw_print_hex32(t, info->tls_kex_params_hash32);
    term_window_print(t, " V:");
    tw_print_hex32(t, info->tls_kex_sig_v32);
    term_window_putc(t, '\n');
    term_window_print(t, "KSig:");
    tw_print_num(t, info->tls_kex_sig_match);
    term_window_print(t, " In:");
    tw_print_num(t, info->tls_kex_sig_verify_inputs);
    term_window_putc(t, '\n');
}

static void tw_print_cke_diag(term_window_t *t) {
    const net_info_t *info = net_info();

    term_window_print(t, "CKE:");
    tw_print_num(t, info->tls_client_key_exchange);
    term_window_print(t, " Ack:");
    tw_print_num(t, info->tls_client_key_exchange_acked);
    term_window_putc(t, '\n');
    term_window_print(t, "Len:");
    tw_print_num(t, info->tls_client_key_exchange_len);
    term_window_print(t, " Stage:");
    tw_print_num(t, info->tls_stage);
    term_window_putc(t, '\n');
    term_window_print(t, "Pub:");
    tw_print_num(t, info->tls_client_pubkey_valid);
    term_window_print(t, " X:");
    tw_print_hex32(t, info->tls_client_pubkey_x32);
    term_window_putc(t, '\n');
    term_window_print(t, "Y:");
    tw_print_hex32(t, info->tls_client_pubkey_y32);
    term_window_putc(t, '\n');
    term_window_print(t, "KSig:");
    tw_print_num(t, info->tls_kex_sig_match);
    term_window_print(t, " Tx:");
    tw_print_num(t, info->tcp_payload_tx);
    term_window_putc(t, '\n');
}

static void tw_print_keys_diag(term_window_t *t) {
    const net_info_t *info = net_info();

    term_window_print(t, "Shared:");
    tw_print_num(t, info->tls_shared_secret_valid);
    term_window_print(t, " X:");
    tw_print_hex32(t, info->tls_shared_secret_x32);
    term_window_putc(t, '\n');
    term_window_print(t, "Master:");
    tw_print_num(t, info->tls_master_secret_valid);
    term_window_print(t, " M0:");
    tw_print_hex32(t, info->tls_master_secret_0);
    term_window_putc(t, '\n');
    term_window_print(t, "M1:");
    tw_print_hex32(t, info->tls_master_secret_1);
    term_window_print(t, " Stage:");
    tw_print_num(t, info->tls_stage);
    term_window_putc(t, '\n');
    term_window_print(t, "KB:");
    tw_print_num(t, info->tls_key_block_valid);
    term_window_print(t, " L:");
    tw_print_num(t, info->tls_key_block_len);
    term_window_putc(t, '\n');
    term_window_print(t, "CW:");
    tw_print_hex32(t, info->tls_client_write_key32);
    term_window_print(t, " SW:");
    tw_print_hex32(t, info->tls_server_write_key32);
    term_window_putc(t, '\n');
    term_window_print(t, "CIV:");
    tw_print_hex32(t, info->tls_client_write_iv32);
    term_window_print(t, " SIV:");
    tw_print_hex32(t, info->tls_server_write_iv32);
    term_window_putc(t, '\n');
    term_window_print(t, "CKE:");
    tw_print_num(t, info->tls_client_key_exchange);
    term_window_print(t, " KSig:");
    tw_print_num(t, info->tls_kex_sig_match);
    term_window_putc(t, '\n');
}

static void tw_print_fin_diag(term_window_t *t) {
    const net_info_t *info = net_info();

    term_window_print(t, "TH:");
    tw_print_hex32(t, info->tls_handshake_hash32);
    term_window_print(t, " TL:");
    tw_print_num(t, info->tls_transcript_len);
    term_window_putc(t, '\n');
    term_window_print(t, "Ovf:");
    tw_print_num(t, info->tls_transcript_overflow);
    term_window_print(t, " Fin:");
    tw_print_num(t, info->tls_client_finished_valid);
    term_window_putc(t, '\n');
    term_window_print(t, "F0:");
    tw_print_hex32(t, info->tls_client_finished0);
    term_window_print(t, " F1:");
    tw_print_hex32(t, info->tls_client_finished1);
    term_window_putc(t, '\n');
    term_window_print(t, "F2:");
    tw_print_hex32(t, info->tls_client_finished2);
    term_window_print(t, " Stage:");
    tw_print_num(t, info->tls_stage);
    term_window_putc(t, '\n');
    term_window_print(t, "KB:");
    tw_print_num(t, info->tls_key_block_valid);
    term_window_print(t, " CKE:");
    tw_print_num(t, info->tls_client_key_exchange);
    term_window_putc(t, '\n');
}

static void tw_print_finish_tx_diag(term_window_t *t) {
    const net_info_t *info = net_info();

    term_window_print(t, "CCS:");
    tw_print_num(t, info->tls_ccs_tx);
    term_window_print(t, " FinTx:");
    tw_print_num(t, info->tls_finished_tx);
    term_window_putc(t, '\n');
    term_window_print(t, "Ack:");
    tw_print_num(t, info->tls_finished_acked);
    term_window_print(t, " RL:");
    tw_print_num(t, info->tls_finished_record_len);
    term_window_putc(t, '\n');
    term_window_print(t, "Tag:");
    tw_print_hex32(t, info->tls_finished_tag32);
    term_window_print(t, " Stage:");
    tw_print_num(t, info->tls_stage);
    term_window_putc(t, '\n');
    term_window_print(t, "SCCS:");
    tw_print_num(t, info->tls_server_ccs_rx);
    term_window_print(t, " SFin:");
    tw_print_num(t, info->tls_server_finished_rx);
    term_window_putc(t, '\n');
    term_window_print(t, "Dec:");
    tw_print_num(t, info->tls_server_finished_decrypt);
    term_window_print(t, " Ver:");
    tw_print_num(t, info->tls_server_finished_verify);
    term_window_putc(t, '\n');
    term_window_print(t, "SF0:");
    tw_print_hex32(t, info->tls_server_finished0);
    term_window_print(t, " Tag:");
    tw_print_hex32(t, info->tls_server_finished_tag32);
    term_window_putc(t, '\n');
    term_window_print(t, "Fin:");
    tw_print_num(t, info->tls_client_finished_valid);
    term_window_print(t, " KB:");
    tw_print_num(t, info->tls_key_block_valid);
    term_window_putc(t, '\n');
}

static void tw_print_tls_app_diag(term_window_t *t) {
    const net_info_t *info = net_info();

    term_window_print(t, "AppTx:");
    tw_print_num(t, info->tls_app_tx);
    term_window_print(t, " Ack:");
    tw_print_num(t, info->tls_app_acked);
    term_window_putc(t, '\n');
    term_window_print(t, "AppRx:");
    tw_print_num(t, info->tls_app_rx);
    term_window_print(t, " Dec:");
    tw_print_num(t, info->tls_app_decrypt);
    term_window_putc(t, '\n');
    term_window_print(t, "Plain:");
    tw_print_num(t, info->tls_app_plain_len);
    term_window_print(t, " RL:");
    tw_print_num(t, info->tls_app_record_len);
    term_window_putc(t, '\n');
    term_window_print(t, "Tag:");
    tw_print_hex32(t, info->tls_app_tag32);
    term_window_print(t, " Stage:");
    tw_print_num(t, info->tls_stage);
    term_window_putc(t, '\n');
    term_window_print(t, "Tx:");
    tw_print_num(t, info->tcp_payload_tx);
    term_window_print(t, " Rx:");
    tw_print_num(t, info->tcp_payload_rx);
    term_window_putc(t, '\n');
    term_window_print(t, "HTTP:");
    tw_print_num(t, info->http_status);
    term_window_print(t, " Bytes:");
    tw_print_num(t, info->http_response_len);
    term_window_putc(t, '\n');
    term_window_print(t, "Resp:");
    tw_print_num(t, info->tls_app_response_len);
    term_window_print(t, " RTag:");
    tw_print_hex32(t, info->tls_app_response_tag32);
    term_window_putc(t, '\n');
    term_window_print(t, "Pend:");
    tw_print_num(t, info->tls_app_pending_rx);
    term_window_putc(t, '/');
    tw_print_num(t, info->tls_app_pending_len);
    term_window_print(t, " HTTP:");
    tw_print_num(t, info->http_valid);
    term_window_putc(t, '\n');
    term_window_print(t, "FinV:");
    tw_print_num(t, info->tls_server_finished_verify);
    term_window_print(t, " Host:");
    term_window_print(t, info->http_host);
    term_window_putc(t, '\n');
}

static void tw_print_p256_diag(term_window_t *t) {
    uint32_t flags = p256_selftest();
    term_window_print(t, "P256 flags:");
    tw_print_hex32(t, flags);
    term_window_putc(t, '\n');
    term_window_print(t, "Add:");
    tw_print_num(t, (flags & 1u) ? 1u : 0u);
    term_window_print(t, " Sub:");
    tw_print_num(t, (flags & 2u) ? 1u : 0u);
    term_window_putc(t, '\n');
    term_window_print(t, "Mul:");
    tw_print_num(t, (flags & 4u) ? 1u : 0u);
    term_window_print(t, " Red:");
    tw_print_num(t, (flags & 8u) ? 1u : 0u);
    term_window_putc(t, '\n');
    term_window_print(t, "Inv:");
    tw_print_num(t, (flags & 16u) ? 1u : 0u);
    term_window_putc(t, '\n');
    term_window_print(t, "G:");
    tw_print_num(t, (flags & 32u) ? 1u : 0u);
    term_window_print(t, " Dbl:");
    tw_print_num(t, (flags & 64u) ? 1u : 0u);
    term_window_putc(t, '\n');
    term_window_print(t, "PAdd:");
    tw_print_num(t, (flags & 128u) ? 1u : 0u);
    term_window_print(t, " SMul:");
    tw_print_num(t, (flags & 256u) ? 1u : 0u);
    term_window_putc(t, '\n');
    term_window_print(t, "S256:");
    tw_print_num(t, (flags & 512u) ? 1u : 0u);
    term_window_print(t, " Z:");
    tw_print_num(t, (flags & 1024u) ? 1u : 0u);
    term_window_print(t, " PJ:");
    tw_print_num(t, (flags & 32768u) ? 1u : 0u);
    term_window_putc(t, '\n');
    term_window_print(t, "NRed:");
    tw_print_num(t, (flags & 2048u) ? 1u : 0u);
    term_window_print(t, " NAdd:");
    tw_print_num(t, (flags & 4096u) ? 1u : 0u);
    term_window_putc(t, '\n');
    term_window_print(t, "NMul:");
    tw_print_num(t, (flags & 8192u) ? 1u : 0u);
    term_window_print(t, " NInv:");
    tw_print_num(t, (flags & 16384u) ? 1u : 0u);
    term_window_putc(t, '\n');
}

static void tw_print_cert_diag(term_window_t *t) {
    const net_info_t *info = net_info();
    rtc_time_t now;
    uint8_t date_ok = tw_cert_date_ok(info, &now);
    if (!info->tls_certificate) {
        term_window_print(t, "No cert\n");
        term_window_print(t, "Run net tls\n");
        return;
    }

    term_window_print(t, "Host:");
    term_window_print(t, info->http_host);
    term_window_putc(t, '\n');
    term_window_print(t, "Chain:");
    tw_print_num(t, info->tls_certificate_list_len);
    term_window_print(t, " Leaf:");
    tw_print_num(t, info->tls_first_certificate_len);
    term_window_putc(t, '\n');
    term_window_print(t, "C2:");
    tw_print_num(t, info->tls_second_certificate_len);
    term_window_print(t, " Link:");
    tw_print_num(t, info->tls_chain_link);
    term_window_putc(t, '\n');
    term_window_print(t, "DER:");
    tw_print_num(t, info->tls_first_certificate_der);
    term_window_print(t, " DL:");
    tw_print_num(t, info->tls_first_certificate_der_len);
    term_window_putc(t, '\n');
    term_window_print(t, "TBS:");
    tw_print_num(t, info->tls_x509_tbs);
    term_window_print(t, " TL:");
    tw_print_num(t, info->tls_x509_tbs_len);
    term_window_print(t, " Ser:");
    tw_print_num(t, info->tls_x509_serial_len);
    term_window_putc(t, '\n');
    term_window_print(t, "Sig:");
    tw_print_num(t, info->tls_x509_sig_alg);
    term_window_print(t, " OID:");
    tw_print_num(t, info->tls_x509_sig_oid_len);
    term_window_putc(t, '\n');
    term_window_print(t, "OSig:");
    tw_print_num(t, info->tls_x509_outer_sig_alg);
    term_window_print(t, " OID:");
    tw_print_num(t, info->tls_x509_outer_sig_oid_len);
    term_window_putc(t, '\n');
    term_window_print(t, "SVal:");
    tw_print_num(t, info->tls_x509_signature_len);
    term_window_print(t, " U:");
    tw_print_num(t, info->tls_x509_signature_unused_bits);
    term_window_putc(t, '\n');
    term_window_print(t, "TBSH:");
    tw_print_hex32(t, info->tls_x509_tbs_hash32);
    term_window_print(t, " H:");
    tw_print_num(t, info->tls_x509_tbs_hash_alg);
    term_window_putc(t, '\n');
    term_window_print(t, "Val:");
    tw_print_num(t, info->tls_x509_validity);
    term_window_print(t, " NB:");
    tw_print_hex8(t, info->tls_x509_not_before_tag);
    term_window_putc(t, ':');
    tw_print_num(t, info->tls_x509_not_before_len);
    term_window_print(t, " NA:");
    tw_print_hex8(t, info->tls_x509_not_after_tag);
    term_window_putc(t, ':');
    tw_print_num(t, info->tls_x509_not_after_len);
    term_window_putc(t, '\n');
    term_window_print(t, "NBd:");
    tw_print_num(t, info->tls_x509_not_before_date);
    term_window_print(t, " NAd:");
    tw_print_num(t, info->tls_x509_not_after_date);
    term_window_putc(t, '\n');
    term_window_print(t, "PK:");
    tw_print_num(t, info->tls_x509_pubkey_alg);
    term_window_print(t, " POID:");
    tw_print_num(t, info->tls_x509_pubkey_oid_len);
    term_window_putc(t, '\n');
    term_window_print(t, "Iss:");
    term_window_print(t, info->tls_x509_issuer_cn);
    term_window_putc(t, '\n');
    term_window_print(t, "IssOK:");
    tw_print_num(t, info->tls_x509_known_issuer);
    term_window_putc(t, '\n');
    term_window_print(t, "CSub:");
    term_window_print(t, info->tls_x509_chain_subject_cn);
    term_window_putc(t, '\n');
    term_window_print(t, "Sub:");
    term_window_print(t, info->tls_x509_subject_cn);
    term_window_putc(t, '\n');
    term_window_print(t, "SAN:");
    term_window_print(t, info->tls_x509_san_dns);
    term_window_putc(t, '\n');
    term_window_print(t, "HostOK:");
    tw_print_num(t, info->tls_x509_host_match);
    term_window_print(t, " BC:");
    tw_print_num(t, info->tls_x509_basic_constraints);
    term_window_print(t, " CA:");
    tw_print_num(t, info->tls_x509_is_ca);
    term_window_print(t, " KU:");
    tw_print_num(t, info->tls_x509_key_usage_bits);
    term_window_print(t, " EKU:");
    tw_print_num(t, info->tls_x509_eku_bits);
    term_window_putc(t, '\n');
    term_window_print(t, "DateOK:");
    tw_print_check(t, date_ok);
    term_window_print(t, " Now:");
    if (date_ok != 2 || now.yyyymmdd) {
        tw_print_num(t, now.yyyymmdd);
    } else {
        term_window_putc(t, '?');
    }
    term_window_putc(t, '\n');
    term_window_print(t, "CertRx:");
    tw_print_num(t, info->tls_certificate_rx);
    term_window_print(t, "/");
    tw_print_num(t, info->tls_certificate_len);
    term_window_putc(t, '\n');
}

static int starts_with(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s++ != *prefix++) return 0;
    }
    return 1;
}

typedef struct {
    char name[9];
    char marker[13];
    char summary[40];
} pkg_entry_t;

static int tw_streq_ci(const char *left, const char *right) {
    while (*left && *right) {
        if (tw_lower(*left++) != tw_lower(*right++)) return 0;
    }
    return *left == 0 && *right == 0;
}

static int tw_copy_pkg_field(const char **cursor,
                             const char *line_end,
                             char delimiter,
                             char *out,
                             uint8_t out_size) {
    uint8_t len = 0;
    const char *p = *cursor;

    if (!out_size) return 0;
    while (p < line_end && (!delimiter || *p != delimiter)) {
        if (len + 1 < out_size) {
            out[len++] = *p;
        }
        p++;
    }
    out[len] = 0;

    if (delimiter) {
        if (p >= line_end || *p != delimiter) return 0;
        p++;
    }

    *cursor = p;
    return len > 0;
}

static int tw_parse_pkg_line(const char *line_start,
                             const char *line_end,
                             pkg_entry_t *entry) {
    const char *cursor = line_start;

    if (line_start >= line_end || *line_start == '#') return 0;
    if (line_end > line_start && line_end[-1] == '\r') line_end--;

    memset(entry, 0, sizeof(pkg_entry_t));
    if (!tw_copy_pkg_field(&cursor, line_end, '|', entry->name, sizeof(entry->name))) return 0;
    if (!tw_copy_pkg_field(&cursor, line_end, '|', entry->marker, sizeof(entry->marker))) return 0;
    if (!tw_copy_pkg_field(&cursor, line_end, 0, entry->summary, sizeof(entry->summary))) return 0;
    return 1;
}

static int tw_read_pkg_manifest(uint32_t *out_size) {
    uint32_t size = 0;

    if (out_size) *out_size = 0;
    if (!fs_is_ready()) return 0;
    if (!fs_read_file("PKGS.TXT", file_buffer, sizeof(file_buffer) - 1, &size)) return 0;
    file_buffer[size] = 0;
    if (out_size) *out_size = size;
    return 1;
}

static int tw_find_pkg(const char *name, pkg_entry_t *out_entry) {
    uint32_t size = 0;
    const char *cursor;
    const char *end;

    if (!name || !*name || !tw_read_pkg_manifest(&size)) return 0;
    cursor = (const char*)file_buffer;
    end = cursor + size;
    while (cursor < end) {
        const char *line_end = cursor;
        pkg_entry_t entry;
        while (line_end < end && *line_end != '\n') line_end++;
        if (tw_parse_pkg_line(cursor, line_end, &entry) &&
            tw_streq_ci(entry.name, name)) {
            if (out_entry) memcpy(out_entry, &entry, sizeof(pkg_entry_t));
            return 1;
        }
        cursor = line_end < end ? line_end + 1 : line_end;
    }

    return 0;
}

static int tw_pkg_installed(const pkg_entry_t *entry) {
    fs_file_t file;
    if (!entry || !fs_open(entry->marker, &file)) return 0;
    fs_close(&file);
    return 1;
}

static void tw_pkg_list(term_window_t *t) {
    uint32_t size = 0;
    const char *cursor;
    const char *end;

    if (!tw_read_pkg_manifest(&size)) {
        term_window_print(t, "pkg no manifest\n");
        return;
    }

    term_window_print(t, "Pkgs:\n");
    cursor = (const char*)file_buffer;
    end = cursor + size;
    while (cursor < end) {
        const char *line_end = cursor;
        pkg_entry_t entry;
        while (line_end < end && *line_end != '\n') line_end++;
        if (tw_parse_pkg_line(cursor, line_end, &entry)) {
            term_window_putc(t, tw_pkg_installed(&entry) ? '*' : ' ');
            term_window_print(t, entry.name);
            term_window_putc(t, ' ');
            term_window_print(t, entry.summary);
            term_window_putc(t, '\n');
        }
        cursor = line_end < end ? line_end + 1 : line_end;
    }
}

static void tw_pkg_info(term_window_t *t, const char *name) {
    pkg_entry_t entry;

    if (!tw_find_pkg(name, &entry)) {
        term_window_print(t, "pkg missing\n");
        return;
    }

    term_window_print(t, entry.name);
    term_window_putc(t, '\n');
    term_window_print(t, entry.summary);
    term_window_putc(t, '\n');
    term_window_print(t, "File:");
    term_window_print(t, entry.marker);
    term_window_putc(t, '\n');
    term_window_print(t, tw_pkg_installed(&entry) ? "installed\n" : "available\n");
}

static void tw_pkg_status(term_window_t *t) {
    uint32_t size = 0;
    const char *cursor;
    const char *end;

    if (!tw_read_pkg_manifest(&size)) {
        term_window_print(t, "pkg no manifest\n");
        return;
    }

    cursor = (const char*)file_buffer;
    end = cursor + size;
    while (cursor < end) {
        const char *line_end = cursor;
        pkg_entry_t entry;
        while (line_end < end && *line_end != '\n') line_end++;
        if (tw_parse_pkg_line(cursor, line_end, &entry)) {
            term_window_print(t, entry.name);
            term_window_putc(t, ':');
            term_window_print(t, tw_pkg_installed(&entry) ? "installed\n" : "available\n");
        }
        cursor = line_end < end ? line_end + 1 : line_end;
    }
}

static void tw_pkg_append(char *buffer, uint16_t *pos, uint16_t cap, const char *text) {
    while (*text && *pos + 1 < cap) {
        buffer[(*pos)++] = *text++;
    }
    buffer[*pos] = 0;
}

static void tw_pkg_install(term_window_t *t, const char *name) {
    pkg_entry_t entry;
    char receipt[128];
    uint16_t pos = 0;

    if (!tw_find_pkg(name, &entry)) {
        term_window_print(t, "pkg missing\n");
        return;
    }
    if (tw_pkg_installed(&entry)) {
        term_window_print(t, "already installed\n");
        return;
    }

    memset(receipt, 0, sizeof(receipt));
    tw_pkg_append(receipt, &pos, sizeof(receipt), "Package:");
    tw_pkg_append(receipt, &pos, sizeof(receipt), entry.name);
    tw_pkg_append(receipt, &pos, sizeof(receipt), "\nSummary:");
    tw_pkg_append(receipt, &pos, sizeof(receipt), entry.summary);
    tw_pkg_append(receipt, &pos, sizeof(receipt), "\nState:installed\n");

    if (!fs_write(entry.marker, (const uint8_t*)receipt, pos,
                  FS_WRITE_CREATE | FS_WRITE_EXCL)) {
        term_window_print(t, "install failed\n");
        return;
    }

    term_window_print(t, "installed ");
    term_window_print(t, entry.name);
    term_window_putc(t, '\n');
}

static void tw_pkg_remove(term_window_t *t, const char *name) {
    pkg_entry_t entry;

    if (!tw_find_pkg(name, &entry)) {
        term_window_print(t, "pkg missing\n");
        return;
    }
    if (!tw_pkg_installed(&entry)) {
        term_window_print(t, "not installed\n");
        return;
    }
    if (!fs_delete_file(entry.marker)) {
        term_window_print(t, "remove failed\n");
        return;
    }

    term_window_print(t, "removed ");
    term_window_print(t, entry.name);
    term_window_putc(t, '\n');
}

typedef struct {
    char name[9];
    char launcher[12];
    char target[64];
    char summary[40];
} app_entry_t;

static int tw_parse_app_line(const char *line_start,
                             const char *line_end,
                             app_entry_t *entry) {
    const char *cursor = line_start;

    if (line_start >= line_end || *line_start == '#') return 0;
    if (line_end > line_start && line_end[-1] == '\r') line_end--;

    memset(entry, 0, sizeof(app_entry_t));
    if (!tw_copy_pkg_field(&cursor, line_end, '|', entry->name, sizeof(entry->name))) return 0;
    if (!tw_copy_pkg_field(&cursor, line_end, '|', entry->launcher, sizeof(entry->launcher))) return 0;
    if (!tw_copy_pkg_field(&cursor, line_end, '|', entry->target, sizeof(entry->target))) return 0;
    if (!tw_copy_pkg_field(&cursor, line_end, 0, entry->summary, sizeof(entry->summary))) return 0;
    return 1;
}

static int tw_read_app_manifest(uint32_t *out_size) {
    uint32_t size = 0;

    if (out_size) *out_size = 0;
    if (!fs_is_ready()) return 0;
    if (!fs_read_file("APPS.TXT", file_buffer, sizeof(file_buffer) - 1, &size)) return 0;
    file_buffer[size] = 0;
    if (out_size) *out_size = size;
    return 1;
}

static int tw_find_app(const char *name, app_entry_t *out_entry) {
    uint32_t size = 0;
    const char *cursor;
    const char *end;

    if (!name || !*name || !tw_read_app_manifest(&size)) return 0;
    cursor = (const char*)file_buffer;
    end = cursor + size;
    while (cursor < end) {
        const char *line_end = cursor;
        app_entry_t entry;
        while (line_end < end && *line_end != '\n') line_end++;
        if (tw_parse_app_line(cursor, line_end, &entry) &&
            tw_streq_ci(entry.name, name)) {
            if (out_entry) memcpy(out_entry, &entry, sizeof(app_entry_t));
            return 1;
        }
        cursor = line_end < end ? line_end + 1 : line_end;
    }

    return 0;
}

static void tw_app_list(term_window_t *t) {
    uint32_t size = 0;
    const char *cursor;
    const char *end;

    if (!tw_read_app_manifest(&size)) {
        term_window_print(t, "app no manifest\n");
        return;
    }

    term_window_print(t, "Apps:\n");
    cursor = (const char*)file_buffer;
    end = cursor + size;
    while (cursor < end) {
        const char *line_end = cursor;
        app_entry_t entry;
        while (line_end < end && *line_end != '\n') line_end++;
        if (tw_parse_app_line(cursor, line_end, &entry)) {
            term_window_print(t, entry.name);
            term_window_putc(t, ' ');
            term_window_print(t, entry.summary);
            term_window_putc(t, '\n');
        }
        cursor = line_end < end ? line_end + 1 : line_end;
    }
}

static void tw_app_info(term_window_t *t, const char *name) {
    app_entry_t entry;

    if (!tw_find_app(name, &entry)) {
        term_window_print(t, "app missing\n");
        return;
    }

    term_window_print(t, entry.name);
    term_window_putc(t, '\n');
    term_window_print(t, entry.summary);
    term_window_putc(t, '\n');
    term_window_print(t, "Launch:");
    term_window_print(t, entry.launcher);
    term_window_putc(t, '\n');
    term_window_print(t, "Target:");
    term_window_print(t, entry.target);
    term_window_putc(t, '\n');
}

static int tw_app_launcher_known(const char *launcher) {
    return tw_streq_ci(launcher, "terminal") ||
           tw_streq_ci(launcher, "editor") ||
           tw_streq_ci(launcher, "viewer") ||
           tw_streq_ci(launcher, "audio") ||
           tw_streq_ci(launcher, "browser");
}

static int tw_app_target_usable(const app_entry_t *entry) {
    const char *target = entry->target;
    fs_file_t file;

    if (target[0] == '-' && target[1] == 0) target = "";
    if (tw_streq_ci(entry->launcher, "terminal")) return 1;
    if (tw_streq_ci(entry->launcher, "editor")) return 1;
    if (tw_streq_ci(entry->launcher, "browser")) return *target != 0;
    if (tw_streq_ci(entry->launcher, "viewer") ||
        tw_streq_ci(entry->launcher, "audio")) {
        if (!*target || !fs_open(target, &file)) return 0;
        fs_close(&file);
        return 1;
    }
    return 0;
}

static void tw_app_check(term_window_t *t) {
    uint32_t size = 0;
    uint32_t ok = 0;
    uint32_t bad = 0;
    const char *cursor;
    const char *end;

    if (!tw_read_app_manifest(&size)) {
        term_window_print(t, "app no manifest\n");
        return;
    }

    cursor = (const char*)file_buffer;
    end = cursor + size;
    while (cursor < end) {
        const char *line_end = cursor;
        app_entry_t entry;
        while (line_end < end && *line_end != '\n') line_end++;
        if (tw_parse_app_line(cursor, line_end, &entry)) {
            if (tw_app_launcher_known(entry.launcher) &&
                tw_app_target_usable(&entry)) {
                ok++;
            } else {
                if (bad < 3) {
                    term_window_print(t, "Bad:");
                    term_window_print(t, entry.name);
                    term_window_putc(t, '\n');
                }
                bad++;
            }
        }
        cursor = line_end < end ? line_end + 1 : line_end;
    }

    term_window_print(t, "Apps ok:");
    tw_print_num(t, ok);
    term_window_print(t, " bad:");
    tw_print_num(t, bad);
    term_window_putc(t, '\n');
}

static void tw_app_run(term_window_t *t, const char *name) {
    app_entry_t entry;
    const char *target;

    if (!tw_find_app(name, &entry)) {
        term_window_print(t, "app missing\n");
        return;
    }

    target = entry.target;
    if (target[0] == '-' && target[1] == 0) target = "";

    if (tw_streq_ci(entry.launcher, "terminal")) {
        term_window_print(t, "terminal active\n");
    } else if (tw_streq_ci(entry.launcher, "editor")) {
        if (!*target) target = "NOTE.TXT";
        if (text_editor_open_file(82, 20, target)) {
            term_window_print(t, "app EDIT\n");
        } else {
            term_window_print(t, "app failed\n");
        }
    } else if (tw_streq_ci(entry.launcher, "viewer")) {
        if (!*target) target = "TEST.BMP";
        if (image_viewer_open_file(96, 28, target)) {
            term_window_print(t, "app VIEW\n");
        } else {
            term_window_print(t, "app failed\n");
        }
    } else if (tw_streq_ci(entry.launcher, "audio")) {
        audio_player_t *player;
        if (!*target) target = "AUDIO.WAV";
        player = audio_player_open_file(92, 42, target);
        if (player) {
            audio_player_play_preview(player);
            term_window_print(t, "app AUDIO\n");
        } else {
            term_window_print(t, "app failed\n");
        }
    } else if (tw_streq_ci(entry.launcher, "browser")) {
        if (!*target) target = "https;//example.com/";
        if (browser_open_url(76, 26, target)) {
            term_window_print(t, "app BROWSER\n");
        } else {
            term_window_print(t, "app wait\n");
        }
    } else {
        term_window_print(t, "launcher missing\n");
    }
}

static const char *tw_app_read_word(const char *cursor, char *out, uint8_t out_size) {
    uint8_t len = 0;

    while (*cursor == ' ') cursor++;
    if (!out_size) return cursor;
    while (*cursor && *cursor != ' ') {
        if (len + 1 < out_size) {
            out[len++] = *cursor;
        }
        cursor++;
    }
    out[len] = 0;
    while (*cursor == ' ') cursor++;
    return cursor;
}

static int tw_app_field_ok(const char *text) {
    if (!text || !*text) return 0;
    while (*text) {
        if (*text == '|' || *text == '\r' || *text == '\n') return 0;
        text++;
    }
    return 1;
}

static void tw_app_add(term_window_t *t, const char *args) {
    app_entry_t entry;
    char line[160];
    uint16_t pos = 0;
    const char *summary;

    memset(&entry, 0, sizeof(entry));
    args = tw_app_read_word(args, entry.name, sizeof(entry.name));
    args = tw_app_read_word(args, entry.launcher, sizeof(entry.launcher));
    args = tw_app_read_word(args, entry.target, sizeof(entry.target));
    summary = args;

    if (!fs_is_ready()) {
        term_window_print(t, "No filesystem\n");
        return;
    }
    if (!tw_app_field_ok(entry.name) ||
        !tw_app_field_ok(entry.launcher) ||
        !tw_app_field_ok(entry.target) ||
        !tw_app_field_ok(summary)) {
        term_window_print(t, "usage app add ID L T S\n");
        return;
    }
    if (tw_find_app(entry.name, 0)) {
        term_window_print(t, "app exists\n");
        return;
    }

    memset(line, 0, sizeof(line));
    tw_pkg_append(line, &pos, sizeof(line), entry.name);
    tw_pkg_append(line, &pos, sizeof(line), "|");
    tw_pkg_append(line, &pos, sizeof(line), entry.launcher);
    tw_pkg_append(line, &pos, sizeof(line), "|");
    tw_pkg_append(line, &pos, sizeof(line), entry.target);
    tw_pkg_append(line, &pos, sizeof(line), "|");
    tw_pkg_append(line, &pos, sizeof(line), summary);
    tw_pkg_append(line, &pos, sizeof(line), "\n");

    if (!fs_write("APPS.TXT", (const uint8_t*)line, pos, FS_WRITE_APPEND)) {
        term_window_print(t, "app add failed\n");
        return;
    }

    term_window_print(t, "app added ");
    term_window_print(t, entry.name);
    term_window_putc(t, '\n');
}

static void tw_app_remove(term_window_t *t, const char *name) {
    uint32_t size = 0;
    uint32_t out_size = 0;
    uint8_t removed = 0;
    const char *cursor;
    const char *end;

    if (!name || !*name) {
        term_window_print(t, "usage app remove NAME\n");
        return;
    }
    if (!tw_read_app_manifest(&size)) {
        term_window_print(t, "app no manifest\n");
        return;
    }

    memset(app_write_buffer, 0, sizeof(app_write_buffer));
    cursor = (const char*)file_buffer;
    end = cursor + size;
    while (cursor < end) {
        const char *line_end = cursor;
        app_entry_t entry;
        uint32_t line_len;

        while (line_end < end && *line_end != '\n') line_end++;
        line_len = (uint32_t)(line_end - cursor);

        if (tw_parse_app_line(cursor, line_end, &entry) &&
            tw_streq_ci(entry.name, name)) {
            removed = 1;
        } else if (line_len > 0) {
            if (out_size + line_len + 1 >= sizeof(app_write_buffer)) {
                term_window_print(t, "app file full\n");
                return;
            }
            memcpy(app_write_buffer + out_size, cursor, line_len);
            out_size += line_len;
            app_write_buffer[out_size++] = '\n';
        }

        cursor = line_end < end ? line_end + 1 : line_end;
    }

    if (!removed) {
        term_window_print(t, "app missing\n");
        return;
    }
    if (!fs_write("APPS.TXT", app_write_buffer, out_size,
                  FS_WRITE_CREATE | FS_WRITE_TRUNCATE)) {
        term_window_print(t, "app remove failed\n");
        return;
    }

    term_window_print(t, "app removed ");
    term_window_print(t, name);
    term_window_putc(t, '\n');
}

static void tw_print_sdk(term_window_t *t) {
    uint32_t size = 0;

    if (fs_is_ready() &&
        fs_read_file("SDK.TXT", file_buffer, sizeof(file_buffer) - 1, &size)) {
        file_buffer[size] = 0;
        term_window_print(t, (const char*)file_buffer);
        if (size == 0 || file_buffer[size - 1] != '\n') {
            term_window_putc(t, '\n');
        }
        return;
    }

    term_window_print(t, "App SDK v1\n");
    term_window_print(t, "APPS.TXT rows:\n");
    term_window_print(t, "ID|launch|target|text\n");
    term_window_print(t, "launchers: editor\n");
    term_window_print(t, "viewer audio browser\n");
    term_window_print(t, "app add ID L T S\n");
}

static void tw_ls_entry(const char *name, uint32_t size, void *ctx) {
    term_window_t *t = (term_window_t*)ctx;
    term_window_print(t, name);
    term_window_putc(t, ' ');
    tw_print_num(t, size);
    term_window_putc(t, '\n');
}

static void tw_task_entry(uint32_t id, const char *name, uint32_t runs, void *ctx) {
    term_window_t *t = (term_window_t*)ctx;
    tw_print_num(t, id);
    term_window_putc(t, ' ');
    term_window_print(t, name);
    term_window_putc(t, ' ');
    tw_print_num(t, runs);
    term_window_putc(t, '\n');
}

static void tw_process_task_id(term_window_t *t, uint32_t task_id) {
    if (task_id == PROCESS_NO_TASK) {
        term_window_putc(t, '-');
    } else {
        tw_print_num(t, task_id);
    }
}

static void tw_process_entry(const process_t *process, void *ctx) {
    term_window_t *t = (term_window_t*)ctx;
    uint32_t running_task = scheduler_running_task_id();
    const char *state = process_state_name(process->state);
    if (process->task_id != PROCESS_NO_TASK && process->task_id == running_task) {
        state = process_state_name(PROCESS_RUNNING);
    }

    tw_print_num(t, process->pid);
    term_window_putc(t, ' ');
    term_window_print(t, process->kernel ? "ker" : "usr");
    term_window_putc(t, ' ');
    tw_process_task_id(t, process->task_id);
    term_window_putc(t, ' ');
    term_window_print(t, state);
    term_window_putc(t, ' ');
    term_window_print(t, process->name);
    term_window_putc(t, '\n');
}

static void tw_context_entry(uint32_t id,
                             const char *name,
                             uint32_t esp,
                             uint32_t irq_esp,
                             uint32_t resume_irq_esp,
                             uint32_t stack_base,
                             uint32_t stack_top,
                             void *ctx) {
    term_window_t *t = (term_window_t*)ctx;
    (void)name;
    (void)resume_irq_esp;
    (void)stack_base;
    (void)stack_top;
    tw_print_num(t, id);
    term_window_putc(t, ' ');
    tw_print_hex32(t, esp);
    term_window_putc(t, ' ');
    tw_print_hex32(t, irq_esp);
    term_window_putc(t, '\n');
}

static void tw_user_process_entry(const process_t *process, void *ctx) {
    if (process->kernel) return;

    term_window_t *t = (term_window_t*)ctx;
    term_window_print(t, "PID:");
    tw_print_num(t, process->pid);
    term_window_putc(t, ' ');
    term_window_print(t, process_state_name(process->state));
    term_window_putc(t, '\n');
    term_window_print(t, "ENT:");
    tw_print_hex32(t, process->user_context.eip);
    term_window_putc(t, '\n');
    term_window_print(t, "STK:");
    tw_print_hex32(t, process->user_context.esp);
    term_window_putc(t, '\n');
    term_window_print(t, "SYS:");
    tw_print_num(t, process->user_context.syscall_count);
    term_window_putc(t, '\n');
}

static void tw_user_context_entry(const process_t *process, void *ctx) {
    if (process->kernel) return;

    term_window_t *t = (term_window_t*)ctx;
    term_window_print(t, "EIP:");
    tw_print_hex32(t, process->user_context.eip);
    term_window_putc(t, '\n');
    term_window_print(t, "ESP:");
    tw_print_hex32(t, process->user_context.esp);
    term_window_putc(t, '\n');
    term_window_print(t, "CS:");
    tw_print_hex32(t, process->user_context.cs);
    term_window_putc(t, ' ');
    term_window_print(t, "SS:");
    tw_print_hex32(t, process->user_context.ss);
    term_window_putc(t, '\n');
    term_window_print(t, "FL:");
    tw_print_hex32(t, process->user_context.eflags);
    term_window_putc(t, '\n');
    term_window_print(t, "AX:");
    tw_print_hex32(t, process->user_context.last_syscall_eax);
    term_window_putc(t, '\n');
    term_window_print(t, "SC:");
    tw_print_num(t, process->user_context.syscall_count);
    term_window_putc(t, '\n');
    if (process->user_context.fault_vector) {
        term_window_print(t, "FV:");
        tw_print_num(t, process->user_context.fault_vector);
        term_window_putc(t, ' ');
        term_window_print(t, "FE:");
        tw_print_hex32(t, process->user_context.fault_error);
        term_window_putc(t, '\n');
        term_window_print(t, "FA:");
        tw_print_hex32(t, process->user_context.fault_address);
        term_window_putc(t, '\n');
        term_window_print(t, "FI:");
        tw_print_hex32(t, process->user_context.fault_eip);
        term_window_putc(t, '\n');
    }
}

static void tw_prepare_user_test(term_window_t *t, const char *label) {
    user_context_t user_context;
    usermode_fill_test_context(&user_context, 0, 0, 0);
    int pid = process_record_user("usertest", &user_context, PROCESS_READY);
    term_window_print(t, label);
    if (pid < 0) {
        term_window_putc(t, '-');
    } else {
        tw_print_num(t, (uint32_t)pid);
    }
    term_window_putc(t, '\n');
}

static void tw_exec(term_window_t *t) {
    const char *cmd = t->input;
    serial_write(cmd);
    serial_write("\n");

    if (strcmp(cmd, "help") == 0) {
        term_window_print(t, "help clear about echo\n");
        term_window_print(t, "meminfo ps ctx uctx\n");
        term_window_print(t, "tss tasks syscall\n");
        term_window_print(t, "date\n");
        term_window_print(t, "usertest userprep\n");
        term_window_print(t, "userreset usersched\n");
        term_window_print(t, "userfault ufault\n");
        term_window_print(t, "userrun usched uproc\n");
        term_window_print(t, "ls cat edit view\n");
        term_window_print(t, "play touch write\n");
        term_window_print(t, "append truncate delete\n");
        term_window_print(t, "rename net net tx\n");
        term_window_print(t, "net rx net arp\n");
        term_window_print(t, "net dns net tcp\n");
        term_window_print(t, "net tcp dns\n");
        term_window_print(t, "net http net page\n");
        term_window_print(t, "net http host/path\n");
        term_window_print(t, "net tls host/path\n");
        term_window_print(t, "net cert trust sig kex cke keys fin\n");
        term_window_print(t, "net finish\n");
        term_window_print(t, "net app\n");
        term_window_print(t, "net p256\n");
        term_window_print(t, "browser theme\n");
        term_window_print(t, "theme list next\n");
        term_window_print(t, "theme classic night\n");
        term_window_print(t, "pkg list info install\n");
        term_window_print(t, "pkg status remove\n");
        term_window_print(t, "app list info run add\n");
        term_window_print(t, "app remove check\n");
        term_window_print(t, "sdk\n");
        term_window_print(t, "preempt reboot\n");
    } else if (strcmp(cmd, "clear") == 0) {
        memset(t->buf, 0, sizeof(t->buf));
        t->cur_col = 0;
        t->cur_row = 0;
    } else if (strcmp(cmd, "about") == 0) {
        term_window_print(t, "MinervaOS v0.3\n");
        term_window_print(t, "Phase 3: Desktop\n");
        term_window_print(t, "x86 32-bit, 320x200\n");
    } else if (strcmp(cmd, "echo") == 0) {
        term_window_print(t, "Hello, MinervaOS!\n");
    } else if (strcmp(cmd, "theme") == 0) {
        tw_print_theme(t);
    } else if (strcmp(cmd, "theme list") == 0) {
        tw_print_theme_list(t);
    } else if (strcmp(cmd, "theme next") == 0) {
        theme_next();
        term_window_print(t, "theme ");
        term_window_print(t, theme_name());
        term_window_putc(t, '\n');
    } else if (starts_with(cmd, "theme ")) {
        if (theme_set(cmd + 6)) {
            term_window_print(t, "theme ");
            term_window_print(t, theme_name());
            term_window_putc(t, '\n');
        } else {
            term_window_print(t, "theme unknown\n");
        }
    } else if (strcmp(cmd, "pkg") == 0) {
        term_window_print(t, "pkg list\n");
        term_window_print(t, "pkg status\n");
        term_window_print(t, "pkg info NAME\n");
        term_window_print(t, "pkg install NAME\n");
        term_window_print(t, "pkg remove NAME\n");
    } else if (strcmp(cmd, "pkg list") == 0) {
        tw_pkg_list(t);
    } else if (strcmp(cmd, "pkg status") == 0) {
        tw_pkg_status(t);
    } else if (starts_with(cmd, "pkg info ")) {
        tw_pkg_info(t, cmd + 9);
    } else if (starts_with(cmd, "pkg install ")) {
        tw_pkg_install(t, cmd + 12);
    } else if (starts_with(cmd, "pkg remove ")) {
        tw_pkg_remove(t, cmd + 11);
    } else if (strcmp(cmd, "app") == 0) {
        term_window_print(t, "app list\n");
        term_window_print(t, "app info NAME\n");
        term_window_print(t, "app run NAME\n");
        term_window_print(t, "app add ID L T S\n");
        term_window_print(t, "app remove NAME\n");
        term_window_print(t, "app check\n");
    } else if (strcmp(cmd, "app list") == 0) {
        tw_app_list(t);
    } else if (strcmp(cmd, "app check") == 0) {
        tw_app_check(t);
    } else if (starts_with(cmd, "app info ")) {
        tw_app_info(t, cmd + 9);
    } else if (starts_with(cmd, "app run ")) {
        tw_app_run(t, cmd + 8);
    } else if (starts_with(cmd, "app add ")) {
        tw_app_add(t, cmd + 8);
    } else if (starts_with(cmd, "app remove ")) {
        tw_app_remove(t, cmd + 11);
    } else if (strcmp(cmd, "sdk") == 0) {
        tw_print_sdk(t);
    } else if (strcmp(cmd, "meminfo") == 0) {
        term_window_print(t, "Total:");
        tw_print_num(t, pmm_total_pages() * 4);
        term_window_print(t, "K\n");
        term_window_print(t, "Used:");
        tw_print_num(t, pmm_used_pages() * 4);
        term_window_print(t, "K\n");
        term_window_print(t, "Free:");
        tw_print_num(t, pmm_free_pages() * 4);
        term_window_print(t, "K\n");
    } else if (strcmp(cmd, "date") == 0) {
        rtc_time_t now;
        if (rtc_read_time(&now)) {
            tw_print_num(t, now.yyyymmdd);
            term_window_putc(t, ' ');
            tw_print_num(t, now.hour);
            term_window_putc(t, ':');
            tw_print_num(t, now.minute);
            term_window_putc(t, ':');
            tw_print_num(t, now.second);
            term_window_putc(t, '\n');
        } else {
            term_window_print(t, "RTC invalid\n");
        }
    } else if (strcmp(cmd, "ps") == 0) {
        term_window_print(t, "PID K TID STATE NAME\n");
        process_list(tw_process_entry, t);
    } else if (strcmp(cmd, "ctx") == 0) {
        term_window_print(t, "ID ESP IRQ\n");
        scheduler_list_contexts(tw_context_entry, t);
    } else if (strcmp(cmd, "tss") == 0) {
        term_window_print(t, "UC:");
        tw_print_hex32(t, gdt_user_code_selector());
        term_window_putc(t, '\n');
        term_window_print(t, "UD:");
        tw_print_hex32(t, gdt_user_data_selector());
        term_window_putc(t, '\n');
        term_window_print(t, "TSS:");
        tw_print_hex32(t, gdt_tss_selector());
        term_window_putc(t, '\n');
        term_window_print(t, "LD:");
        tw_print_num(t, gdt_tss_loaded());
        term_window_putc(t, '\n');
        term_window_print(t, "SS0:");
        tw_print_hex32(t, gdt_tss_ss0());
        term_window_putc(t, '\n');
        term_window_print(t, "ESP0:");
        tw_print_hex32(t, gdt_tss_esp0());
        term_window_putc(t, '\n');
    } else if (strcmp(cmd, "syscall") == 0) {
        term_window_print(t, "Count:");
        tw_print_num(t, syscall_get_count());
        term_window_putc(t, '\n');
        term_window_print(t, "EAX:");
        tw_print_hex32(t, syscall_get_last_eax());
        term_window_putc(t, '\n');
        term_window_print(t, "CS:");
        tw_print_hex32(t, syscall_get_last_cs());
        term_window_putc(t, '\n');
        term_window_print(t, "User:");
        tw_print_num(t, syscall_get_user_count());
        term_window_putc(t, '\n');
        term_window_print(t, "Ret:");
        tw_print_hex32(t, syscall_get_last_result());
        term_window_putc(t, '\n');
    } else if (strcmp(cmd, "syscall test") == 0) {
        uint32_t result = syscall_test_interrupt();
        term_window_print(t, "Ret:");
        tw_print_hex32(t, result);
        term_window_putc(t, '\n');
        term_window_print(t, "Count:");
        tw_print_num(t, syscall_get_count());
        term_window_putc(t, '\n');
    } else if (strcmp(cmd, "usertest") == 0) {
        uint32_t result = usermode_run_test();
        user_context_t user_context;
        usermode_fill_test_context(&user_context,
                                   syscall_get_last_eax(),
                                   syscall_get_last_cs(),
                                   syscall_get_user_count());
        process_record_user("usertest", &user_context, PROCESS_ZOMBIE);
        term_window_print(t, "UserRet:");
        tw_print_hex32(t, result);
        term_window_putc(t, '\n');
        term_window_print(t, "CS:");
        tw_print_hex32(t, syscall_get_last_cs());
        term_window_putc(t, '\n');
        term_window_print(t, "EAX:");
        tw_print_hex32(t, syscall_get_last_eax());
        term_window_putc(t, '\n');
        term_window_print(t, "User:");
        tw_print_num(t, syscall_get_user_count());
        term_window_putc(t, '\n');
    } else if (strcmp(cmd, "userprep") == 0) {
        tw_prepare_user_test(t, "Ready PID:");
    } else if (strcmp(cmd, "userreset") == 0) {
        tw_prepare_user_test(t, "Reset PID:");
    } else if (strcmp(cmd, "userrun") == 0) {
        user_context_t user_context;
        if (!process_get_user_context("usertest", &user_context)) {
            term_window_print(t, "No user process\n");
        } else {
            process_record_user("usertest", &user_context, PROCESS_RUNNING);
            uint32_t result = usermode_run_context(&user_context);
            usermode_fill_test_context(&user_context,
                                       syscall_get_last_eax(),
                                       syscall_get_last_cs(),
                                       syscall_get_user_count());
            process_record_user("usertest", &user_context, PROCESS_ZOMBIE);
            term_window_print(t, "UserRet:");
            tw_print_hex32(t, result);
            term_window_putc(t, '\n');
            term_window_print(t, "User:");
            tw_print_num(t, syscall_get_user_count());
            term_window_putc(t, '\n');
        }
    } else if (strcmp(cmd, "userfault") == 0) {
        user_context_t user_context;
        usermode_fill_fault_context(&user_context, 0, 0, 0, 0);
        process_record_user("userfault", &user_context, PROCESS_RUNNING);
        uint32_t result = usermode_run_context(&user_context);
        usermode_fill_fault_context(&user_context,
                                    user_fault_get_vector(),
                                    user_fault_get_eip(),
                                    user_fault_get_address(),
                                    user_fault_get_error());
        process_record_user("userfault", &user_context, PROCESS_ZOMBIE);
        term_window_print(t, "FaultRet:");
        tw_print_hex32(t, result);
        term_window_putc(t, '\n');
        term_window_print(t, "Vec:");
        tw_print_num(t, user_fault_get_vector());
        term_window_putc(t, '\n');
        term_window_print(t, "Addr:");
        tw_print_hex32(t, user_fault_get_address());
        term_window_putc(t, '\n');
        term_window_print(t, "Err:");
        tw_print_hex32(t, user_fault_get_error());
        term_window_putc(t, '\n');
    } else if (strcmp(cmd, "ufault") == 0) {
        term_window_print(t, "Count:");
        tw_print_num(t, user_fault_get_count());
        term_window_putc(t, '\n');
        term_window_print(t, "Vec:");
        tw_print_num(t, user_fault_get_vector());
        term_window_putc(t, '\n');
        term_window_print(t, "CS:");
        tw_print_hex32(t, user_fault_get_cs());
        term_window_putc(t, '\n');
        term_window_print(t, "EIP:");
        tw_print_hex32(t, user_fault_get_eip());
        term_window_putc(t, '\n');
        term_window_print(t, "Addr:");
        tw_print_hex32(t, user_fault_get_address());
        term_window_putc(t, '\n');
        term_window_print(t, "Err:");
        tw_print_hex32(t, user_fault_get_error());
        term_window_putc(t, '\n');
    } else if (strcmp(cmd, "usersched") == 0) {
        if (user_scheduler_arm()) {
            term_window_print(t, "USched armed\n");
        } else {
            term_window_print(t, "No READY user\n");
        }
    } else if (strcmp(cmd, "usched") == 0) {
        term_window_print(t, "Armed:");
        tw_print_num(t, user_scheduler_armed());
        term_window_putc(t, '\n');
        term_window_print(t, "Runs:");
        tw_print_num(t, user_scheduler_runs());
        term_window_putc(t, '\n');
        term_window_print(t, "Launch:");
        tw_print_num(t, user_scheduler_launches());
        term_window_putc(t, '\n');
        term_window_print(t, "Idle:");
        tw_print_num(t, user_scheduler_idle_count());
        term_window_putc(t, '\n');
        term_window_print(t, "NoReady:");
        tw_print_num(t, user_scheduler_no_ready_count());
        term_window_putc(t, '\n');
        term_window_print(t, "PID:");
        tw_print_num(t, user_scheduler_last_pid());
        term_window_putc(t, '\n');
        term_window_print(t, "Ret:");
        tw_print_hex32(t, user_scheduler_last_result());
        term_window_putc(t, '\n');
    } else if (strcmp(cmd, "uproc") == 0) {
        process_list(tw_user_process_entry, t);
    } else if (strcmp(cmd, "uctx") == 0) {
        process_list(tw_user_context_entry, t);
    } else if (starts_with(cmd, "net http ")) {
        if (net_http_get_url(cmd + 9)) {
            term_window_print(t, "HTTP ok\n");
        } else {
            term_window_print(t, "HTTP wait\n");
        }
        tw_print_http_diag(t);
    } else if (strcmp(cmd, "net http") == 0) {
        if (net_http_get_example()) {
            term_window_print(t, "HTTP ok\n");
        } else {
            term_window_print(t, "HTTP wait\n");
        }
        tw_print_http_diag(t);
    } else if (strcmp(cmd, "net page") == 0) {
        const net_info_t *info = net_info();
        if (!info->http_response_len) {
            net_http_get_example();
            info = net_info();
        }
        if (!info->http_response_len) {
            term_window_print(t, "No page\n");
        } else {
            tw_print_http_page(t, info);
        }
    } else if (starts_with(cmd, "net tls ")) {
        if (net_tls_probe_url(cmd + 8)) {
            term_window_print(t, "TLS hello ok\n");
        } else {
            term_window_print(t, "TLS wait\n");
        }
        tw_print_tls_diag(t);
    } else if (strcmp(cmd, "net tls") == 0) {
        if (net_tls_probe_example()) {
            term_window_print(t, "TLS hello ok\n");
        } else {
            term_window_print(t, "TLS wait\n");
        }
        tw_print_tls_diag(t);
    } else if (strcmp(cmd, "net cert") == 0) {
        if (!net_info()->tls_certificate) {
            net_tls_probe_example();
        }
        tw_print_cert_diag(t);
    } else if (strcmp(cmd, "net trust") == 0) {
        if (!net_info()->tls_certificate) {
            net_tls_probe_example();
        }
        tw_print_trust_diag(t);
    } else if (strcmp(cmd, "net sig") == 0) {
        if (!net_info()->tls_certificate) {
            net_tls_probe_example();
        }
        if (!net_info()->tls_x509_ecdsa_point_done) {
            net_tls_verify_signature();
        }
        tw_print_sig_diag(t);
    } else if (strcmp(cmd, "net kex") == 0) {
        if (!net_info()->tls_server_key_exchange) {
            net_tls_probe_example();
        }
        tw_print_kex_diag(t);
    } else if (strcmp(cmd, "net cke") == 0) {
        if (!net_info()->tls_server_key_exchange) {
            net_tls_probe_example();
        }
        if (!net_info()->tls_kex_sig_point_done) {
            net_tls_verify_kex_signature();
        }
        net_tls_send_client_key_exchange();
        tw_print_cke_diag(t);
    } else if (strcmp(cmd, "net keys") == 0) {
        if (!net_info()->tls_client_key_exchange) {
            if (!net_info()->tls_server_key_exchange) {
                net_tls_probe_example();
            }
            if (!net_info()->tls_kex_sig_point_done) {
                net_tls_verify_kex_signature();
            }
            net_tls_send_client_key_exchange();
        }
        net_tls_derive_keys();
        tw_print_keys_diag(t);
    } else if (strcmp(cmd, "net fin") == 0) {
        if (!net_info()->tls_key_block_valid) {
            if (!net_info()->tls_client_key_exchange) {
                if (!net_info()->tls_server_key_exchange) {
                    net_tls_probe_example();
                }
                if (!net_info()->tls_kex_sig_point_done) {
                    net_tls_verify_kex_signature();
                }
                net_tls_send_client_key_exchange();
            }
            net_tls_derive_keys();
        }
        net_tls_compute_finished();
        tw_print_fin_diag(t);
    } else if (strcmp(cmd, "net finish") == 0) {
        if (!net_info()->tls_client_finished_valid) {
            if (!net_info()->tls_key_block_valid) {
                if (!net_info()->tls_client_key_exchange) {
                    if (!net_info()->tls_server_key_exchange) {
                        net_tls_probe_example();
                    }
                    if (!net_info()->tls_kex_sig_point_done) {
                        net_tls_verify_kex_signature();
                    }
                    net_tls_send_client_key_exchange();
                }
                net_tls_derive_keys();
            }
            net_tls_compute_finished();
        }
        net_tls_send_finished();
        tw_print_finish_tx_diag(t);
    } else if (strcmp(cmd, "net app") == 0) {
        if (!net_info()->tls_server_finished_verify) {
            if (!net_info()->tls_client_finished_valid) {
                if (!net_info()->tls_key_block_valid) {
                    if (!net_info()->tls_client_key_exchange) {
                        if (!net_info()->tls_server_key_exchange) {
                            net_tls_probe_example();
                        }
                        if (!net_info()->tls_kex_sig_point_done) {
                            net_tls_verify_kex_signature();
                        }
                        net_tls_send_client_key_exchange();
                    }
                    net_tls_derive_keys();
                }
                net_tls_compute_finished();
            }
            net_tls_send_finished();
        }
        net_tls_send_http_get();
        tw_print_tls_app_diag(t);
    } else if (strcmp(cmd, "net p256") == 0) {
        tw_print_p256_diag(t);
    } else if (starts_with(cmd, "browser ")) {
        browser_t *browser = browser_open_url(72, 22, cmd + 8);
        if (browser && net_info()->http_response_len) {
            term_window_print(t, "Browser ok\n");
        } else {
            term_window_print(t, "Browser wait\n");
        }
    } else if (strcmp(cmd, "browser") == 0) {
        browser_t *browser = browser_open(72, 22);
        if (browser && net_info()->http_response_len) {
            term_window_print(t, "Browser ok\n");
        } else {
            term_window_print(t, "Browser wait\n");
        }
    } else if (strcmp(cmd, "net tcp") == 0 || strcmp(cmd, "net tcp dns") == 0) {
        int ok = strcmp(cmd, "net tcp dns") == 0 ?
                 net_tcp_connect_dns() :
                 net_tcp_connect_example();
        if (ok) {
            term_window_print(t, "TCP ok\n");
        } else {
            term_window_print(t, "TCP wait\n");
        }
        const net_info_t *info = net_info();
        term_window_print(t, "Syn:");
        tw_print_num(t, info->tcp_syns);
        term_window_putc(t, '\n');
        term_window_print(t, "SA:");
        tw_print_num(t, info->tcp_synacks);
        term_window_putc(t, '\n');
        term_window_print(t, "Ack:");
        tw_print_num(t, info->tcp_acks);
        term_window_putc(t, '\n');
        term_window_print(t, "Err:");
        tw_print_num(t, info->tcp_errors);
        term_window_putc(t, '\n');
        term_window_print(t, "Rx:");
        tw_print_num(t, info->rx_frames);
        term_window_putc(t, '\n');
        term_window_print(t, "Eth:");
        tw_print_hex32(t, info->last_ethertype);
        term_window_putc(t, '\n');
        term_window_print(t, "Tgt:");
        tw_print_ip(t, info->tcp_target_ip);
        term_window_putc(t, ':');
        tw_print_num(t, info->tcp_target_port);
        term_window_putc(t, '\n');
        term_window_print(t, "LP:");
        tw_print_num(t, info->tcp_local_port);
        term_window_putc(t, '\n');
        term_window_print(t, "P:");
        tw_print_num(t, info->last_tcp_src);
        term_window_putc(t, '>');
        tw_print_num(t, info->last_tcp_dst);
        term_window_putc(t, '\n');
        term_window_print(t, "Fl:");
        tw_print_hex8(t, info->last_tcp_flags);
        term_window_putc(t, '\n');
        term_window_print(t, "Seq:");
        tw_print_hex32(t, info->last_tcp_seq);
        term_window_putc(t, '\n');
        term_window_print(t, "AckN:");
        tw_print_hex32(t, info->last_tcp_ack);
        term_window_putc(t, '\n');
    } else if (strcmp(cmd, "net dns") == 0) {
        if (net_dns_query_example()) {
            term_window_print(t, "DNS ok\n");
        } else {
            term_window_print(t, "DNS wait\n");
        }
        const net_info_t *info = net_info();
        term_window_print(t, "Q:");
        tw_print_num(t, info->dns_queries);
        term_window_putc(t, '\n');
        term_window_print(t, "R:");
        tw_print_num(t, info->dns_replies);
        term_window_putc(t, '\n');
        term_window_print(t, "DNS:");
        tw_print_ip(t, info->dns_ip);
        term_window_putc(t, '\n');
        term_window_print(t, "IPs:");
        tw_print_num(t, info->dns_ip_count);
        term_window_putc(t, '\n');
        term_window_print(t, "Rx:");
        tw_print_num(t, info->rx_frames);
        term_window_putc(t, '\n');
        term_window_print(t, "Eth:");
        tw_print_hex32(t, info->last_ethertype);
        term_window_putc(t, '\n');
        term_window_print(t, "UDP:");
        tw_print_num(t, info->last_udp_src);
        term_window_putc(t, '>');
        tw_print_num(t, info->last_udp_dst);
        term_window_putc(t, '\n');
        term_window_print(t, "DID:");
        tw_print_hex32(t, info->last_dns_id);
        term_window_putc(t, '\n');
        term_window_print(t, "DF:");
        tw_print_hex32(t, info->last_dns_flags);
        term_window_putc(t, '\n');
        term_window_print(t, "A:");
        if (info->dns_last_ip_valid) {
            tw_print_ip(t, info->dns_last_ip);
            term_window_putc(t, '\n');
        } else {
            term_window_print(t, "none\n");
        }
    } else if (strcmp(cmd, "net arp") == 0) {
        if (net_arp_probe_gateway()) {
            term_window_print(t, "ARP ok\n");
        } else {
            term_window_print(t, "ARP wait\n");
        }
        const net_info_t *info = net_info();
        term_window_print(t, "Req:");
        tw_print_num(t, info->arp_requests);
        term_window_putc(t, '\n');
        term_window_print(t, "Rep:");
        tw_print_num(t, info->arp_replies);
        term_window_putc(t, '\n');
        term_window_print(t, "Rx:");
        tw_print_num(t, info->rx_frames);
        term_window_putc(t, '\n');
        term_window_print(t, "Gw:");
        if (info->gateway_mac_valid) {
            tw_print_mac(t, info->gateway_mac);
            term_window_putc(t, '\n');
        } else {
            term_window_print(t, "unknown\n");
        }
    } else if (strcmp(cmd, "net tx") == 0) {
        if (e1000_send_test_frame()) {
            term_window_print(t, "TX sent\n");
        } else {
            term_window_print(t, "TX failed\n");
        }
        const e1000_info_t *net = e1000_info();
        term_window_print(t, "Try:");
        tw_print_num(t, net->tx_attempts);
        term_window_putc(t, '\n');
        term_window_print(t, "Sent:");
        tw_print_num(t, net->tx_sent);
        term_window_putc(t, '\n');
        term_window_print(t, "Err:");
        tw_print_num(t, net->tx_errors);
        term_window_putc(t, '\n');
        term_window_print(t, "Stat:");
        tw_print_hex8(t, net->tx_last_status);
        term_window_putc(t, '\n');
    } else if (strcmp(cmd, "net rx") == 0) {
        if (e1000_poll_receive()) {
            term_window_print(t, "RX packet\n");
        } else {
            term_window_print(t, "RX none\n");
        }
        const e1000_info_t *net = e1000_info();
        term_window_print(t, "Pkts:");
        tw_print_num(t, net->rx_packets);
        term_window_putc(t, '\n');
        term_window_print(t, "Err:");
        tw_print_num(t, net->rx_errors);
        term_window_putc(t, '\n');
        term_window_print(t, "Len:");
        tw_print_num(t, net->rx_last_length);
        term_window_putc(t, '\n');
        term_window_print(t, "Type:");
        tw_print_hex32(t, net->rx_last_type);
        term_window_putc(t, '\n');
        term_window_print(t, "Stat:");
        tw_print_hex8(t, net->rx_last_status);
        term_window_putc(t, ' ');
        term_window_print(t, "Er:");
        tw_print_hex8(t, net->rx_last_errors);
        term_window_putc(t, '\n');
    } else if (strcmp(cmd, "net") == 0) {
        const e1000_info_t *net = e1000_info();
        term_window_print(t, "e1000:");
        term_window_print(t, net->present ? "yes\n" : "no\n");
        if (net->present) {
            term_window_print(t, "BDF:");
            tw_print_num(t, net->pci.bus);
            term_window_putc(t, ':');
            tw_print_num(t, net->pci.slot);
            term_window_putc(t, '.');
            tw_print_num(t, net->pci.function);
            term_window_putc(t, '\n');
            term_window_print(t, "VID:");
            tw_print_hex32(t, net->pci.vendor_id);
            term_window_putc(t, '\n');
            term_window_print(t, "DID:");
            tw_print_hex32(t, net->pci.device_id);
            term_window_putc(t, '\n');
            term_window_print(t, "BAR0:");
            tw_print_hex32(t, net->pci.bar0);
            term_window_putc(t, '\n');
            term_window_print(t, "TYPE:");
            term_window_print(t, net->mmio ? "MMIO\n" : "IO\n");
            term_window_print(t, "TX:");
            term_window_print(t, net->tx_ready ? "ready\n" : "off\n");
            term_window_print(t, "RX:");
            term_window_print(t, net->rx_ready ? "ready\n" : "off\n");
            const net_info_t *info = net_info();
            term_window_print(t, "IP:");
            tw_print_ip(t, info->local_ip);
            term_window_putc(t, '\n');
            term_window_print(t, "GW:");
            tw_print_ip(t, info->gateway_ip);
            term_window_putc(t, '\n');
            term_window_print(t, "DNS:");
            tw_print_ip(t, info->dns_ip);
            term_window_putc(t, '\n');
            if (net->mac_valid) {
                term_window_print(t, "MAC:");
                tw_print_mac(t, net->mac);
                term_window_putc(t, '\n');
            } else {
                term_window_print(t, "MAC:unread\n");
            }
            term_window_print(t, "ARP:");
            if (info->gateway_mac_valid) {
                tw_print_mac(t, info->gateway_mac);
                term_window_putc(t, '\n');
            } else {
                term_window_print(t, "none\n");
            }
            term_window_print(t, "A:");
            if (info->dns_last_ip_valid) {
                tw_print_ip(t, info->dns_last_ip);
                term_window_putc(t, '\n');
            } else {
                term_window_print(t, "none\n");
            }
        }
    } else if (strcmp(cmd, "tasks") == 0) {
        term_window_print(t, "Switches:");
        tw_print_num(t, scheduler_switch_count());
        term_window_putc(t, '\n');
        term_window_print(t, "TimerReq:");
        tw_print_num(t, scheduler_timer_request_count());
        term_window_putc(t, '\n');
        term_window_print(t, "IRQFrames:");
        tw_print_num(t, scheduler_irq_frame_count());
        term_window_putc(t, '\n');
        term_window_print(t, "IRQCtx:");
        tw_print_num(t, scheduler_irq_context_count());
        term_window_putc(t, '\n');
        term_window_print(t, "Cand:");
        tw_print_num(t, scheduler_irq_candidate_count());
        term_window_putc(t, '\n');
        term_window_print(t, "PMode:");
        tw_print_num(t, scheduler_preemptive_enabled());
        term_window_putc(t, '\n');
        term_window_print(t, "IRQSw:");
        tw_print_num(t, scheduler_irq_preempt_switch_count());
        term_window_putc(t, '\n');
        term_window_print(t, "Block:");
        tw_print_num(t, scheduler_irq_preempt_blocked_count());
        term_window_putc(t, '\n');
        term_window_print(t, "MainCap:");
        tw_print_num(t, scheduler_main_capture_count());
        term_window_putc(t, '\n');
        term_window_print(t, "M->T:");
        tw_print_num(t, scheduler_main_to_task_count());
        term_window_putc(t, '\n');
        term_window_print(t, "I->M:");
        tw_print_num(t, scheduler_irq_to_main_count());
        term_window_putc(t, '\n');
        term_window_print(t, "Y->M:");
        tw_print_num(t, scheduler_yield_to_main_count());
        term_window_putc(t, '\n');
        term_window_print(t, "MSw:");
        tw_print_num(t, scheduler_main_switch_enabled());
        term_window_putc(t, '\n');
        scheduler_list(tw_task_entry, t);
    } else if (strcmp(cmd, "preempt") == 0) {
        term_window_print(t, "PMode:");
        tw_print_num(t, scheduler_preemptive_enabled());
        term_window_putc(t, '\n');
    } else if (strcmp(cmd, "preempt on") == 0) {
        scheduler_set_preemptive_enabled(1);
        term_window_print(t, "PMode:1\n");
    } else if (strcmp(cmd, "preempt off") == 0) {
        scheduler_set_preemptive_enabled(0);
        term_window_print(t, "PMode:0\n");
    } else if (strcmp(cmd, "mainsw on") == 0) {
        scheduler_set_main_switch_enabled(1);
        term_window_print(t, "MSw:1\n");
    } else if (strcmp(cmd, "mainsw off") == 0) {
        scheduler_set_main_switch_enabled(0);
        term_window_print(t, "MSw:0\n");
    } else if (strcmp(cmd, "ls") == 0) {
        if (!fs_is_ready()) {
            term_window_print(t, "No filesystem\n");
        } else if (!fs_list_root(tw_ls_entry, t)) {
            term_window_print(t, "ls failed\n");
        }
    } else if (starts_with(cmd, "cat ")) {
        const char *name = cmd + 4;
        fs_file_t file;
        uint32_t size = 0;
        uint32_t total = 0;
        char last = 0;
        if (!fs_is_ready()) {
            term_window_print(t, "No filesystem\n");
        } else if (!fs_open(name, &file)) {
            term_window_print(t, "cat failed\n");
        } else {
            size = fs_file_size(&file);
            while (total < size) {
                uint32_t got = fs_read(&file, file_buffer, sizeof(file_buffer) - 1);
                if (got == 0) break;
                file_buffer[got] = 0;
                last = (char)file_buffer[got - 1];
                total += got;
                term_window_print(t, (const char*)file_buffer);
            }
            fs_close(&file);
            if (total != size) {
                term_window_print(t, "cat failed\n");
            } else if (size == 0 || last != '\n') {
                term_window_putc(t, '\n');
            }
        }
    } else if (starts_with(cmd, "edit ")) {
        const char *name = cmd + 5;
        if (!*name) {
            term_window_print(t, "edit failed\n");
        } else if (!text_editor_open_file(82, 20, name)) {
            term_window_print(t, "edit failed\n");
        }
    } else if (starts_with(cmd, "view ")) {
        const char *name = cmd + 5;
        if (!*name) {
            term_window_print(t, "view failed\n");
        } else if (!image_viewer_open_file(96, 28, name)) {
            term_window_print(t, "view failed\n");
        }
    } else if (starts_with(cmd, "play ")) {
        const char *name = cmd + 5;
        audio_player_t *player;
        if (!*name) {
            term_window_print(t, "play failed\n");
        } else if (!(player = audio_player_open_file(92, 42, name))) {
            term_window_print(t, "play failed\n");
        } else if (!audio_player_play_preview(player)) {
            term_window_print(t, "No playback\n");
        }
    } else if (starts_with(cmd, "touch ")) {
        const char *name = cmd + 6;
        if (!fs_is_ready()) {
            term_window_print(t, "No filesystem\n");
        } else if (!fs_create(name)) {
            term_window_print(t, "touch failed\n");
        }
    } else if (starts_with(cmd, "write ")) {
        char *name = t->input + 6;
        char *text = name;
        while (*text && *text != ' ') text++;
        if (*text == ' ') {
            *text++ = 0;
        }

        if (!fs_is_ready()) {
            term_window_print(t, "No filesystem\n");
        } else if (!*name || !*text ||
                   !fs_write(name, (const uint8_t*)text, strlen(text),
                             FS_WRITE_CREATE | FS_WRITE_TRUNCATE)) {
            term_window_print(t, "write failed\n");
        }
    } else if (starts_with(cmd, "append ")) {
        char *name = t->input + 7;
        char *text = name;
        while (*text && *text != ' ') text++;
        if (*text == ' ') {
            *text++ = 0;
        }

        if (!fs_is_ready()) {
            term_window_print(t, "No filesystem\n");
        } else if (!*name || !*text ||
                   !fs_write(name, (const uint8_t*)text, strlen(text), FS_WRITE_APPEND)) {
            term_window_print(t, "append failed\n");
        }
    } else if (starts_with(cmd, "truncate ")) {
        const char *name = cmd + 9;
        if (!fs_is_ready()) {
            term_window_print(t, "No filesystem\n");
        } else if (!fs_truncate_file(name)) {
            term_window_print(t, "truncate failed\n");
        }
    } else if (starts_with(cmd, "delete ")) {
        const char *name = cmd + 7;
        if (!fs_is_ready()) {
            term_window_print(t, "No filesystem\n");
        } else if (!fs_delete_file(name)) {
            term_window_print(t, "delete failed\n");
        }
    } else if (starts_with(cmd, "rename ")) {
        char *old_name = t->input + 7;
        char *new_name = old_name;
        while (*new_name && *new_name != ' ') new_name++;
        if (*new_name == ' ') {
            *new_name++ = 0;
        }

        if (!fs_is_ready()) {
            term_window_print(t, "No filesystem\n");
        } else if (!*old_name || !*new_name || !fs_rename_file(old_name, new_name)) {
            term_window_print(t, "rename failed\n");
        }
    } else if (strcmp(cmd, "reboot") == 0) {
        while (inb(0x64) & 0x02) {}
        outb(0x64, 0xFE);
        __asm__ volatile("cli; hlt");
    } else if (cmd[0] != 0) {
        term_window_print(t, "Unknown: ");
        term_window_print(t, cmd);
        term_window_putc(t, '\n');
    }
}

void term_window_handle_key(term_window_t *t, char c) {
    if (c == '\n') {
        t->input[t->input_len] = 0;
        term_window_putc(t, '\n');
        tw_exec(t);
        t->input_len = 0;
        term_window_print(t, "$ ");
    } else if (c == '\b') {
        if (t->input_len > 0) {
            t->input_len--;
            term_window_putc(t, '\b');
        }
    } else if (t->input_len < (int)sizeof(t->input) - 1) {
        t->input[t->input_len++] = c;
        term_window_putc(t, c);
    }
}

void term_window_render(term_window_t *t) {
    if (!t || !t->win || window_is_minimized(t->win)) return;

    /* Black background — window_render() already filled it, but clear
       the client area so old chars don't ghost when the cursor moves back */
    window_clear(t->win, 0);

    for (int r = 0; r < TWIN_ROWS; r++) {
        for (int c = 0; c < TWIN_COLS; c++) {
            char ch = t->buf[r][c];
            if (ch >= 32)
                window_draw_char(t->win,
                                 (uint32_t)(c * 8), (uint32_t)(r * 8),
                                 ch, 10);  /* bright green on black */
        }
    }
    /* Solid block cursor */
    window_fill_rect(t->win,
                     (uint32_t)(t->cur_col * 8), (uint32_t)(t->cur_row * 8),
                     8, 8, 10);
}

term_window_t *term_window_create(uint32_t x, uint32_t y) {
    term_window_t *t = &g_tw;
    /* 200 wide, 100 tall: client = 198 x 82 → 24 cols x 10 rows */
    t->win = window_create(x, y, 200, 100, "Terminal");
    if (!t->win) return NULL;

    window_set_bg_color(t->win, 0);       /* black interior */
    window_set_title_color(t->win, 8, 10); /* dark-gray bar, green label */

    memset(t->buf, 0, sizeof(t->buf));
    t->cur_col   = 0;
    t->cur_row   = 0;
    t->input_len = 0;

    term_window_print(t, "MinervaOS Terminal\n");
    term_window_print(t, "Type 'help'\n");
    term_window_print(t, "$ ");
    return t;
}
