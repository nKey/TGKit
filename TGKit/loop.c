//
//  loop.c
//  TGKit
//
//  Created by Paul Eipper on 24/10/2014.
//  Copyright (c) 2014 nKey. All rights reserved.
//

#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>

#include "loop.h"
#include "binlog.h"
#include "structures.h"
#include <event2/event.h>

#define DC_SERIALIZED_MAGIC 0x868aa81d
#define STATE_FILE_MAGIC 0x28949a93
#define SECRET_CHAT_FILE_MAGIC 0x37a1988a

void _dummy_logprintf(const char *format, ...) {};
void (*logprintf)(const char *format, ...) = _dummy_logprintf;


// config functions

const char *get_downloads_directory (void) {
    return config.get_download_directory ();
}

const char *get_binlog_file_name (void) {
    return config.get_binlog_filename ();
}


// loader functions

void read_state_file (void) {
    int state_file_fd = open (config.get_state_filename (), O_CREAT | O_RDWR, 0600);
    if (state_file_fd < 0) {
        return;
    }
    int version, magic;
    if (read (state_file_fd, &magic, 4) < 4) { close (state_file_fd); return; }
    if (magic != (int)STATE_FILE_MAGIC) { close (state_file_fd); return; }
    if (read (state_file_fd, &version, 4) < 4) { close (state_file_fd); return; }
    assert (version >= 0);
    int x[4];
    if (read (state_file_fd, x, 16) < 16) {
        close (state_file_fd);
        return;
    }
    int pts = x[0];
    int qts = x[1];
    int seq = x[2];
    int date = x[3];
    close (state_file_fd);
    bl_do_set_seq (seq);
    bl_do_set_pts (pts);
    bl_do_set_qts (qts);
    bl_do_set_date (date);
}

void write_state_file (void) {
    static int wseq;
    static int wpts;
    static int wqts;
    static int wdate;
    if (wseq >= tgl_state.seq && wpts >= tgl_state.pts && wqts >= tgl_state.qts && wdate >= tgl_state.date) { return; }
    wseq = tgl_state.seq; wpts = tgl_state.pts; wqts = tgl_state.qts; wdate = tgl_state.date;
    int state_file_fd = open (config.get_state_filename (), O_CREAT | O_RDWR, 0600);
    if (state_file_fd < 0) {
        logprintf ("Can not write state file '%s': %m\n", config.get_state_filename ());
        exit (1);
    }
    int x[6];
    x[0] = STATE_FILE_MAGIC;
    x[1] = 0;
    x[2] = wpts;
    x[3] = wqts;
    x[4] = wseq;
    x[5] = wdate;
    assert (write (state_file_fd, x, 24) == 24);
    close (state_file_fd); 
}

void write_dc (struct tgl_dc *DC, void *extra) {
    int auth_file_fd = *(int *)extra;
    if (!DC) {
        int x = 0;
        assert (write (auth_file_fd, &x, 4) == 4);
        return;
    } else {
        int x = 1;
        assert (write (auth_file_fd, &x, 4) == 4);
    }
    
    assert (DC->has_auth);
    
    assert (write (auth_file_fd, &DC->port, 4) == 4);
    int l = (int)(strlen (DC->ip));
    assert (write (auth_file_fd, &l, 4) == 4);
    assert (write (auth_file_fd, DC->ip, l) == l);
    assert (write (auth_file_fd, &DC->auth_key_id, 8) == 8);
    assert (write (auth_file_fd, DC->auth_key, 256) == 256);
}

void write_auth_file (void) {
    int auth_file_fd = open (config.get_auth_key_filename (), O_CREAT | O_RDWR, 0600);
    assert (auth_file_fd >= 0);
    int x = DC_SERIALIZED_MAGIC;
    assert (write (auth_file_fd, &x, 4) == 4);
    assert (write (auth_file_fd, &tgl_state.max_dc_num, 4) == 4);
    assert (write (auth_file_fd, &tgl_state.dc_working_num, 4) == 4);
    
    tgl_dc_iterator_ex (write_dc, &auth_file_fd);
    
    assert (write (auth_file_fd, &tgl_state.our_id, 4) == 4);
    close (auth_file_fd);
}

void write_secret_chat (tgl_peer_t *_P, void *extra) {
    struct tgl_secret_chat *P = (void *)_P;
    if (tgl_get_peer_type (P->id) != TGL_PEER_ENCR_CHAT) { return; }
    if (P->state != sc_ok) { return; }
    int *a = extra;
    int fd = a[0];
    a[1] ++;
    
    int id = tgl_get_peer_id (P->id);
    assert (write (fd, &id, 4) == 4);
    //assert (write (fd, &P->flags, 4) == 4);
    int l = (int)(strlen (P->print_name));
    assert (write (fd, &l, 4) == 4);
    assert (write (fd, P->print_name, l) == l);
    assert (write (fd, &P->user_id, 4) == 4);
    assert (write (fd, &P->admin_id, 4) == 4);
    assert (write (fd, &P->date, 4) == 4);
    assert (write (fd, &P->ttl, 4) == 4);
    assert (write (fd, &P->layer, 4) == 4);
    assert (write (fd, &P->access_hash, 8) == 8);
    assert (write (fd, &P->state, 4) == 4);
    assert (write (fd, &P->key_fingerprint, 8) == 8);
    assert (write (fd, &P->key, 256) == 256);
}

void write_secret_chat_file (void) {
    int secret_chat_fd = open (config.get_secret_chat_filename (), O_CREAT | O_RDWR, 0600);
    assert (secret_chat_fd >= 0);
    int x = SECRET_CHAT_FILE_MAGIC;
    assert (write (secret_chat_fd, &x, 4) == 4);
    x = 0;
    assert (write (secret_chat_fd, &x, 4) == 4); // version
    assert (write (secret_chat_fd, &x, 4) == 4); // num
    
    int y[2];
    y[0] = secret_chat_fd;
    y[1] = 0;
    
    tgl_peer_iterator_ex (write_secret_chat, y);
    
    lseek (secret_chat_fd, 8, SEEK_SET);
    assert (write (secret_chat_fd, &y[1], 4) == 4);
    close (secret_chat_fd);
}

void read_dc (int auth_file_fd, int id, unsigned ver) {
    int port = 0;
    assert (read (auth_file_fd, &port, 4) == 4);
    int l = 0;
    assert (read (auth_file_fd, &l, 4) == 4);
    assert (l >= 0 && l < 100);
    char ip[100];
    assert (read (auth_file_fd, ip, l) == l);
    ip[l] = 0;
    
    long long auth_key_id;
    static unsigned char auth_key[256];
    assert (read (auth_file_fd, &auth_key_id, 8) == 8);
    assert (read (auth_file_fd, auth_key, 256) == 256);
    
    //bl_do_add_dc (id, ip, l, port, auth_key_id, auth_key);
    bl_do_dc_option (id, 2, "DC", l, ip, port);
    bl_do_set_auth_key_id (id, auth_key);
    bl_do_dc_signed (id);
}

void empty_auth_file (void) {
    if (tgl_state.test_mode) {
        bl_do_dc_option (1, 0, "", strlen (TG_SERVER_TEST_1), TG_SERVER_TEST_1, 443);
        bl_do_dc_option (2, 0, "", strlen (TG_SERVER_TEST_2), TG_SERVER_TEST_2, 443);
        bl_do_dc_option (3, 0, "", strlen (TG_SERVER_TEST_3), TG_SERVER_TEST_3, 443);
        bl_do_set_working_dc (2);
    } else {
        bl_do_dc_option (1, 0, "", strlen (TG_SERVER_1), TG_SERVER_1, 443);
        bl_do_dc_option (2, 0, "", strlen (TG_SERVER_2), TG_SERVER_2, 443);
        bl_do_dc_option (3, 0, "", strlen (TG_SERVER_3), TG_SERVER_3, 443);
        bl_do_dc_option (4, 0, "", strlen (TG_SERVER_4), TG_SERVER_4, 443);
        bl_do_dc_option (5, 0, "", strlen (TG_SERVER_5), TG_SERVER_5, 443);
        bl_do_set_working_dc (2);
    }
}

void read_auth_file (void) {
    int auth_file_fd = open (config.get_auth_key_filename (), O_CREAT | O_RDWR, 0600);
    if (auth_file_fd < 0) {
        empty_auth_file ();
        return;
    }
    assert (auth_file_fd >= 0);
    unsigned x;
    unsigned m;
    if (read (auth_file_fd, &m, 4) < 4 || (m != DC_SERIALIZED_MAGIC)) {
        close (auth_file_fd);
        empty_auth_file ();
        return;
    }
    assert (read (auth_file_fd, &x, 4) == 4);
    assert (x > 0);
    int dc_working_num;
    assert (read (auth_file_fd, &dc_working_num, 4) == 4);
    
    int i;
    for (i = 0; i <= (int)x; i++) {
        int y;
        assert (read (auth_file_fd, &y, 4) == 4);
        if (y) {
            read_dc (auth_file_fd, i, m);
        }
    }
    bl_do_set_working_dc (dc_working_num);
    int our_id;
    int l = (int)(read (auth_file_fd, &our_id, 4));
    if (l < 4) {
        assert (!l);
    }
    if (our_id) {
        bl_do_set_our_id (our_id);
    }
    close (auth_file_fd);
}

void read_secret_chat (int fd) {
    int id, l, user_id, admin_id, date, ttl, layer, state;
    long long access_hash, key_fingerprint;
    static char s[1000];
    static unsigned char key[256];
    assert (read (fd, &id, 4) == 4);
    //assert (read (fd, &flags, 4) == 4);
    assert (read (fd, &l, 4) == 4);
    assert (l > 0 && l < 1000);
    assert (read (fd, s, l) == l);
    assert (read (fd, &user_id, 4) == 4);
    assert (read (fd, &admin_id, 4) == 4);
    assert (read (fd, &date, 4) == 4);
    assert (read (fd, &ttl, 4) == 4);
    assert (read (fd, &layer, 4) == 4);
    assert (read (fd, &access_hash, 8) == 8);
    assert (read (fd, &state, 4) == 4);
    assert (read (fd, &key_fingerprint, 8) == 8);
    assert (read (fd, &key, 256) == 256);
    
    bl_do_encr_chat_create (id, user_id, admin_id, s, l);
    struct tgl_secret_chat  *P = (void *)tgl_peer_get (TGL_MK_ENCR_CHAT (id));
    assert (P && (P->flags & FLAG_CREATED));
    bl_do_encr_chat_set_date (P, date);
    bl_do_encr_chat_set_ttl (P, ttl);
    bl_do_encr_chat_set_layer (P, layer);
    bl_do_encr_chat_set_access_hash (P, access_hash);
    bl_do_encr_chat_set_state (P, state);
    bl_do_encr_chat_set_key (P, key, key_fingerprint);
}

void read_secret_chat_file (void) {
    int secret_chat_fd = open (config.get_secret_chat_filename (), O_RDWR, 0600);
    if (secret_chat_fd < 0) { return; }
    //assert (secret_chat_fd >= 0);
    int x;
    if (read (secret_chat_fd, &x, 4) < 4) { close (secret_chat_fd); return; }
    if (x != SECRET_CHAT_FILE_MAGIC) { close (secret_chat_fd); return; }
    assert (read (secret_chat_fd, &x, 4) == 4);
    assert (!x); // version
    assert (read (secret_chat_fd, &x, 4) == 4);
    assert (x >= 0);
    while (x --> 0) {
        read_secret_chat (secret_chat_fd);
    }
    close (secret_chat_fd);
}


// loop callbacks

int d_got_ok;
void get_difference_callback (void *extra, int success) {
    assert (success);
    d_got_ok = 1;
}

int dgot (void) {
    return d_got_ok;
}

void dlist_cb (void *callback_extra, int success, int size, tgl_peer_id_t peers[], int last_msg_id[], int unread_count[])  {
    d_got_ok = 1;
}

int signed_in_ok;
void sign_in_result (void *extra, int success, struct tgl_user *U) {
    if (!success) {
        logprintf("Can not login");
        exit (1);
    }
    signed_in_ok = 1;
}

int signed_in (void) {
    return signed_in_ok;
}

int should_register;
char *hash;
void sign_in_callback (void *extra, int success, int registered, const char *mhash) {
    if (!success) {
        logprintf("Can not send code");
        exit (1);
    }
    should_register = !registered;
    hash = strdup (mhash);
}

int sent_code (void) {
    return hash != 0;
}

void export_auth_callback (void *DC, int success) {
    if (!success) {
        logprintf ("Can not export auth\n");
        exit (1);
    }
}

struct tgl_dc *cur_a_dc;
int is_authorized (void) {
    return tgl_authorized_dc (cur_a_dc);
}

int all_authorized (void) {
    int i;
    for (i = 0; i <= tgl_state.max_dc_num; i++) if (tgl_state.DC_list[i]) {
        if (!tgl_authorized_dc (tgl_state.DC_list[i])) {
            return 0;
        }
    }
    return 1;
}

int dc_signed_in (void) {
    return tgl_signed_dc (cur_a_dc);
}


// main loops

void net_loop (int flags, int (*is_end)(void)) {
    int last_get_state = (int)(time (0));
    while (!is_end || !is_end ()) {
        event_base_loop (tgl_state.ev_base, EVLOOP_ONCE);
        if (time (0) - last_get_state > 3600) {
            tgl_do_lookup_state ();
            last_get_state = (int)(time (0));
        }
    }
}

void wait_loop(int (*is_end)(void)) {
    net_loop (0, is_end);
}

int main_loop (void) {
    net_loop (1, 0);
    return 0;
}

int loop(struct tgl_update_callback *upd_cb) {
    logprintf = upd_cb->logprintf;
    tgl_set_binlog_mode (0);
    tgl_set_download_directory(config.get_download_directory ());
    tgl_set_callback(upd_cb);
    tgl_init();
    read_auth_file ();
    read_state_file ();
    read_secret_chat_file ();
    if (config.reset_authorization) {
        tgl_peer_t *P = tgl_peer_get (TGL_MK_USER (tgl_state.our_id));
        if (P && P->user.phone && config.reset_authorization == 1) {
            logprintf("Try to login as %s", P->user.phone);
            config.set_default_username(P->user.phone);
        }
        bl_do_reset_authorization ();
    }
    net_loop (0, all_authorized);
    if (!tgl_signed_dc(tgl_state.DC_working)) {
        logprintf("Need to login first");
        tgl_do_send_code(config.get_default_username (), sign_in_callback, 0);
        net_loop(0, sent_code);
        logprintf ("%s\n", should_register ? "phone not registered" : "phone registered");
        if (!should_register) {
            logprintf("Enter SMS code");
            while (1) {
                if (tgl_do_send_code_result (config.get_default_username (), hash, config.get_sms_code (), sign_in_result, 0) >= 0) {
                    break;
                }
                break;
            }
        }
        net_loop (0, signed_in);
    }
    for (int i = 0; i <= tgl_state.max_dc_num; i++) if (tgl_state.DC_list[i] && !tgl_signed_dc (tgl_state.DC_list[i])) {
        tgl_do_export_auth (i, export_auth_callback, (void*)(long)tgl_state.DC_list[i]);
        cur_a_dc = tgl_state.DC_list[i];
        net_loop (0, dc_signed_in);
        assert (tgl_signed_dc (tgl_state.DC_list[i]));
    }
    write_auth_file ();
    tglm_send_all_unsent ();
    tgl_do_get_difference (config.sync_from_start, get_difference_callback, 0);
    net_loop (0, dgot);
    assert (!(tgl_state.locks & TGL_LOCK_DIFF));
    tgl_state.started = 1;
    if (config.wait_dialog_list) {
        d_got_ok = 0;
        tgl_do_get_dialog_list (dlist_cb, 0);
        net_loop (0, dgot);
    }
    return main_loop();
}
