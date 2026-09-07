/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * Archivo derivado del proyecto Proyecto1_RobotYocto---Sistemas-Empotrados,
 * MIT License - Copyright (c) 2026 Javier Tenorio Cervantes. Ver NOTICE.md.
 */

#include "lib_audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <dirent.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <strings.h>  
#include <mpg123.h>
#include <alsa/asoundlib.h>

/* ═══════════════════════════════════════════════════════════
   Comandos para el thread de reproduccion
═══════════════════════════════════════════════════════════ */
typedef enum {
    PCMD_NONE   = 0,
    PCMD_PLAY   = 1,
    PCMD_PAUSE  = 2,
    PCMD_RESUME = 3,
    PCMD_STOP   = 4,
    PCMD_QUIT   = 5,
} PlayCmd;

/* ═══════════════════════════════════════════════════════════
   Parametros Globales
═══════════════════════════════════════════════════════════ */
static struct {
    // Lista de pistas
    LibAudioTrack   tracks[LIB_AUDIO_TRACKS_MAX];
    int             track_count;
    char            audio_dir[512];

    // Thread de reproduccion
    pthread_t        thread;
    pthread_mutex_t  lock;
    pthread_cond_t   cond;
    PlayCmd          cmd;
    char             cmd_filepath[512];
    int              cmd_track_id;

    // Estado actual
    LibAudioStatus   status;
    int              current_id;
    float            position_secs;
    int              volume;          

    // PID del proceso de notificación activo
    // pid_t            notify_pid;

    int              initialized;
} g;

// ═══════════ Helpers PCM ALSA

static snd_pcm_t *alsa_open(long rate, int channels)
{
    snd_pcm_t *pcm = NULL;
    int err;

    err = snd_pcm_open(&pcm, LIB_AUDIO_ALSA_DEVICE,
                       SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        fprintf(stderr, "[audio] snd_pcm_open: %s\n", snd_strerror(err));
        return NULL;
    }

    snd_pcm_hw_params_t *hw;
    snd_pcm_hw_params_alloca(&hw);
    snd_pcm_hw_params_any(pcm, hw);
    snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(pcm, hw, (unsigned int)channels);

    unsigned int r = (unsigned int)rate;
    snd_pcm_hw_params_set_rate_near(pcm, hw, &r, 0);

    // Buffer de 4096 frames para cada track
    snd_pcm_uframes_t buf_sz = 4096;
    snd_pcm_hw_params_set_buffer_size_near(pcm, hw, &buf_sz);

    err = snd_pcm_hw_params(pcm, hw);
    if (err < 0) {
        fprintf(stderr, "[audio] snd_pcm_hw_params: %s\n", snd_strerror(err));
        snd_pcm_close(pcm);
        return NULL;
    }

    snd_pcm_prepare(pcm);
    return pcm;
}

// Ajusta el volumen del mixer ALSA
static void alsa_set_volume(int vol_pct)
{
    if (vol_pct < 0)   vol_pct = 0;
    if (vol_pct > 100) vol_pct = 100;

    snd_mixer_t *handle = NULL;
    if (snd_mixer_open(&handle, 0) < 0) return;

    // Apuntar a card 1: Headphones en rpi4
    if (snd_mixer_attach(handle, LIB_AUDIO_MIXER_CARD) < 0) {
        snd_mixer_close(handle); return;
    }
    snd_mixer_selem_register(handle, NULL, NULL);
    snd_mixer_load(handle);

    snd_mixer_selem_id_t *sid;
    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, LIB_AUDIO_MIXER_CTRL);

    snd_mixer_elem_t *elem = snd_mixer_find_selem(handle, sid);
    if (elem) {
        long pmin, pmax;
        snd_mixer_selem_get_playback_volume_range(elem, &pmin, &pmax);
        long vol = pmin + (long)(((pmax - pmin) * vol_pct) / 100);
        snd_mixer_selem_set_playback_volume_all(elem, vol);
        printf("[audio] Volumen card 1 '%s': %d%% (raw=%ld)\n",
               LIB_AUDIO_MIXER_CTRL, vol_pct, vol);
    } else {
        fprintf(stderr, "[audio] Control '%s' no encontrado en hw:1\n",
                LIB_AUDIO_MIXER_CTRL);
    }

    snd_mixer_close(handle);
}

// Duracion del MP3 usando mpg123
static int mp3_duration(const char *path)
{
    int err_code;
    mpg123_handle *mh = mpg123_new(NULL, &err_code);
    mpg123_param(mh, MPG123_QUIET, 1, 0);  /* suprimir warnings de ID3 malformados */
    if (!mh) return 0;

    int secs = 0;
    if (mpg123_open(mh, path) == MPG123_OK) {
        mpg123_scan(mh);
        off_t  length = mpg123_length(mh);
        long   rate   = 44100;
        int    ch, enc;
        mpg123_getformat(mh, &rate, &ch, &enc);
        if (rate > 0 && length > 0)
            secs = (int)(length / rate);
        mpg123_close(mh);
    }
    mpg123_delete(mh);
    return secs;
}

/* ═══════════════════════════════════════════════════════════
   Main Thread de reproducción principal
═══════════════════════════════════════════════════════════ */
static void *playback_thread(void *arg)
{
    (void)arg;
    mpg123_handle *mh  = NULL;
    snd_pcm_t     *pcm = NULL;

    while (1) {
        pthread_mutex_lock(&g.lock);
        while (g.cmd == PCMD_NONE)
            pthread_cond_wait(&g.cond, &g.lock);

        PlayCmd cmd = g.cmd;
        char    fp[512];
        int     tid = g.cmd_track_id;
        strncpy(fp, g.cmd_filepath, sizeof(fp) - 1);
        fp[511] = '\0';
        g.cmd = PCMD_NONE;
        pthread_mutex_unlock(&g.lock);

        if (cmd == PCMD_QUIT) break;
        if (cmd != PCMD_PLAY) continue; 

        //Abrir archivo MP3
        if (!mh) mh = mpg123_new(NULL, NULL);
        mpg123_param(mh, MPG123_QUIET, 1, 0);  /* suprimir warnings de ID3 */
        if (!mh) { fprintf(stderr, "[audio] mpg123_new failed\n"); continue; }

        if (mpg123_open(mh, fp) != MPG123_OK) {
            fprintf(stderr, "[audio] No se pudo abrir '%s': %s\n",
                    fp, mpg123_strerror(mh));
            continue;
        }

        // Forzar formato S16_LE para ALSA
        long rate = 44100; int channels = 2, encoding = MPG123_ENC_SIGNED_16;
        mpg123_getformat(mh, &rate, &channels, &encoding);
        mpg123_format_none(mh);
        mpg123_format(mh, rate, channels, MPG123_ENC_SIGNED_16);

        // Abrir ALSA
        if (pcm) { snd_pcm_close(pcm); pcm = NULL; }
        pcm = alsa_open(rate, channels);
        if (!pcm) {
            mpg123_close(mh);
            pthread_mutex_lock(&g.lock);
            g.status     = LIB_AUDIO_STOPPED;
            g.current_id = -1;
            pthread_mutex_unlock(&g.lock);
            continue;
        }

        pthread_mutex_lock(&g.lock);
        g.status        = LIB_AUDIO_PLAYING;
        g.current_id    = tid;
        g.position_secs = 0.0f;
        pthread_mutex_unlock(&g.lock);
        printf("[audio] Reproduciendo id=%d '%s'\n", tid, fp);

        // Establecer bucle de decodificacion
        unsigned char buf[8192];
        size_t        done;
        int           dec_err;
        int           stop_requested = 0;

        while (!stop_requested) {

            // Verificar comando pendiente
            pthread_mutex_lock(&g.lock);

            // PAUSA: suspender decodificación hasta RESUME / STOP
            if (g.cmd == PCMD_PAUSE) {
                g.cmd    = PCMD_NONE;
                g.status = LIB_AUDIO_PAUSED;
                // Vaciar el buffer para audio residual
                snd_pcm_drop(pcm);   
                printf("[audio] Pausado en %.1fs\n", g.position_secs);

                while (g.cmd != PCMD_RESUME && g.cmd != PCMD_STOP &&
                       g.cmd != PCMD_PLAY  && g.cmd != PCMD_QUIT)
                    pthread_cond_wait(&g.cond, &g.lock);

                if (g.cmd == PCMD_RESUME) {
                    g.cmd    = PCMD_NONE;
                    g.status = LIB_AUDIO_PLAYING;
                    // Rearmar el ALSA
                    snd_pcm_prepare(pcm);   
                    printf("[audio] Resumido\n");
                }
            }

            /* STOP / nueva cancion / salir */
            if (g.cmd == PCMD_STOP || g.cmd == PCMD_PLAY || g.cmd == PCMD_QUIT) {
                stop_requested = 1;
                if (g.cmd == PCMD_STOP) {
                    g.cmd           = PCMD_NONE;
                    g.status        = LIB_AUDIO_STOPPED;
                    g.current_id    = -1;
                    g.position_secs = 0.0f;
                }
                pthread_mutex_unlock(&g.lock);
                break;
            }

            pthread_mutex_unlock(&g.lock);

            // Decodificar chunk
            dec_err = mpg123_read(mh, buf, sizeof(buf), &done);

            //if (dec_err == MPG123_DONE || done == 0) {
            //    pthread_mutex_lock(&g.lock);
            //    g.status        = LIB_AUDIO_STOPPED;
            //    g.current_id    = -1;
            //    g.position_secs = 0.0f;
            //    pthread_mutex_unlock(&g.lock);
            //    printf("[audio] Fin de pista\n");
            //    break;
            //}

            if (dec_err == MPG123_DONE || done == 0) {
                printf("[audio] Fin de pista id=%d\n", tid);

                pthread_mutex_lock(&g.lock);

                /* Buscar indice actual en la lista */
                int next_id = -1;
                int i;
                for (i = 0; i < g.track_count; i++) {
                    if (g.tracks[i].id == tid) {
                        /* Siguiente pista — si es la ultima vuelve a la primera */
                        int next_idx = (i + 1) % g.track_count;
                        next_id = g.tracks[next_idx].id;
                        strncpy(g.cmd_filepath, g.tracks[next_idx].filepath,
                                sizeof(g.cmd_filepath) - 1);
                        g.cmd_filepath[sizeof(g.cmd_filepath) - 1] = '\0';
                        break;
                    }
                }

                if (next_id >= 0 && g.track_count > 1) {
                    /* Encolar siguiente pista automaticamente */
                    g.cmd_track_id  = next_id;
                    g.cmd           = PCMD_PLAY;
                    g.status        = LIB_AUDIO_STOPPED;
                    g.current_id    = -1;
                    g.position_secs = 0.0f;
                    printf("[audio] Autoplay → siguiente id=%d\n", next_id);
                } else {
                    /* Una sola pista o lista vacia — detener */
                    g.status        = LIB_AUDIO_STOPPED;
                    g.current_id    = -1;
                    g.position_secs = 0.0f;
                }

                pthread_mutex_unlock(&g.lock);
                break;
            }

            if (dec_err != MPG123_OK && dec_err != MPG123_NEW_FORMAT) {
                fprintf(stderr, "[audio] Error de decodificación: %s\n",
                        mpg123_strerror(mh));
                break;
            }

            // Escribir chunk a ALSA
            snd_pcm_uframes_t frames   = snd_pcm_bytes_to_frames(pcm, (ssize_t)done);
            unsigned char    *ptr      = buf;
            snd_pcm_uframes_t written  = 0;

            while (frames > 0) {
                snd_pcm_sframes_t wr = snd_pcm_writei(pcm, ptr, frames);
                if (wr < 0) {
                    wr = snd_pcm_recover(pcm, (int)wr, 0);
                    if (wr < 0) {
                        fprintf(stderr, "[audio] Error ALSA: %s\n", snd_strerror((int)wr));
                        break;
                    }
                    continue;
                }
                ptr     += snd_pcm_frames_to_bytes(pcm, (snd_pcm_uframes_t)wr);
                frames  -= (snd_pcm_uframes_t)wr;
                written += (snd_pcm_uframes_t)wr;
            }

            // Actualizar posición una vez por chunk
            if (written > 0) {
                pthread_mutex_lock(&g.lock);
                g.position_secs += (float)written / (float)rate;
                pthread_mutex_unlock(&g.lock);
            }
        }

        // Terminar la decodificacion
        pthread_mutex_lock(&g.lock);
        int switching = (g.cmd == PCMD_PLAY);
        pthread_mutex_unlock(&g.lock);

        if (switching)
            snd_pcm_drop(pcm);  
        else
            snd_pcm_drain(pcm); 
        mpg123_close(mh);
    }

    if (pcm) { snd_pcm_close(pcm); pcm = NULL; }
    if (mh)  { mpg123_delete(mh);  mh  = NULL; }
    return NULL;
}

/* ═══════════════════════════════════════════════════════════
   API - Inicializacion con parametros iniciales
═══════════════════════════════════════════════════════════ */

int lib_audio_init(const char *audio_dir)
{
    if (g.initialized) return 0;

    memset(&g, 0, sizeof(g));
    strncpy(g.audio_dir,
            audio_dir ? audio_dir : LIB_AUDIO_DIR_DEFAULT,
            sizeof(g.audio_dir) - 1);

    g.volume     = 85;
    g.status     = LIB_AUDIO_STOPPED;
    g.current_id = -1;
    // g.notify_pid = -1;

    mpg123_init();

    if (pthread_mutex_init(&g.lock, NULL) != 0) return -1;
    if (pthread_cond_init (&g.cond, NULL) != 0) return -1;

    g.initialized = 1;

    if (pthread_create(&g.thread, NULL, playback_thread, NULL) != 0) {
        fprintf(stderr, "[audio] No se pudo crear hilo de reproducción\n");
        return -1;
    }

    alsa_set_volume(g.volume);
    lib_audio_scan();

    printf("[audio] Inicializado — dir='%s'  pistas=%d  volumen=%d%%\n",
           g.audio_dir, g.track_count, g.volume);
    return 0;
}

void lib_audio_destroy(void)
{
    if (!g.initialized) return;

    pthread_mutex_lock(&g.lock);
    g.cmd = PCMD_QUIT;
    pthread_cond_signal(&g.cond);
    pthread_mutex_unlock(&g.lock);

    pthread_join(g.thread, NULL);
    pthread_mutex_destroy(&g.lock);
    pthread_cond_destroy(&g.cond);

    //if (g.notify_pid > 0) {
    //    kill(g.notify_pid, SIGTERM);
    //    waitpid(g.notify_pid, NULL, WNOHANG);
    // }

    mpg123_exit();
    g.initialized = 0;
    printf("[audio] destruido\n");
}

/* ═══════════════════════════════════════════════════════════
   API — Escaneo de audios / tracks
═══════════════════════════════════════════════════════════ */

int lib_audio_scan(void)
{
    if (!g.initialized) return -1;

    DIR *dir = opendir(g.audio_dir);
    if (!dir) {
        fprintf(stderr, "[audio] No se pudo abrir '%s'\n", g.audio_dir);
        return -1;
    }

    // Resetear / Actualizar lista
    pthread_mutex_lock(&g.lock);
    g.track_count = 0;
    pthread_mutex_unlock(&g.lock);

    struct dirent *entry;
    int count = 0;

    while ((entry = readdir(dir)) != NULL && count < LIB_AUDIO_TRACKS_MAX) {
        const char *name = entry->d_name;
        size_t      nlen = strlen(name);

        // Solo archivos .mp3
        if (nlen < 5 || strcasecmp(name + nlen - 4, ".mp3") != 0) continue;
        // No tomar en cuenta los archivos que son para notificaciones
        if (strncmp(name, "notify_", 7) == 0) continue;

        LibAudioTrack *t = &g.tracks[count];
        t->id = count + 1;
        strncpy(t->filename, name,         LIB_AUDIO_NAME_MAX - 1);
        t->filename[LIB_AUDIO_NAME_MAX - 1] = '\0';
        snprintf(t->filepath, sizeof(t->filepath), "%s/%s", g.audio_dir, name);
        t->duration_secs = mp3_duration(t->filepath);

        count++;
    }
    closedir(dir);

    pthread_mutex_lock(&g.lock);
    g.track_count = count;
    pthread_mutex_unlock(&g.lock);

    printf("[audio] Escaneo: %d pista(s) en '%s'\n", count, g.audio_dir);
    return count;
}

int lib_audio_get_tracks(LibAudioTrack *out, int max)
{
    if (!g.initialized || !out || max <= 0) return 0;
    pthread_mutex_lock(&g.lock);
    int n = (g.track_count < max) ? g.track_count : max;
    memcpy(out, g.tracks, (size_t)n * sizeof(LibAudioTrack));
    pthread_mutex_unlock(&g.lock);
    return n;
}

/* ═══════════════════════════════════════════════════════════
   API - Control de reproduccion
═══════════════════════════════════════════════════════════ */

int lib_audio_play(int track_id)
{
    if (!g.initialized) return -1;

    pthread_mutex_lock(&g.lock);

    const char *fp = NULL;
    for (int i = 0; i < g.track_count; i++) {
        if (g.tracks[i].id == track_id) { fp = g.tracks[i].filepath; break; }
    }
    if (!fp) {
        pthread_mutex_unlock(&g.lock);
        fprintf(stderr, "[audio] Audio %d no encontrado\n", track_id);
        return -1;
    }

    strncpy(g.cmd_filepath, fp, sizeof(g.cmd_filepath) - 1);
    g.cmd_filepath[sizeof(g.cmd_filepath) - 1] = '\0';
    g.cmd_track_id = track_id;
    g.cmd          = PCMD_PLAY;
    pthread_cond_signal(&g.cond);
    pthread_mutex_unlock(&g.lock);
    return 0;
}

void lib_audio_pause(void)
{
    if (!g.initialized) return;
    pthread_mutex_lock(&g.lock);
    if (g.status == LIB_AUDIO_PLAYING) {
        g.cmd = PCMD_PAUSE;
        pthread_cond_signal(&g.cond);
    }
    pthread_mutex_unlock(&g.lock);
}

void lib_audio_resume(void)
{
    if (!g.initialized) return;
    pthread_mutex_lock(&g.lock);
    if (g.status == LIB_AUDIO_PAUSED) {
        g.cmd = PCMD_RESUME;
        pthread_cond_signal(&g.cond);
    }
    pthread_mutex_unlock(&g.lock);
}

void lib_audio_stop(void)
{
    if (!g.initialized) return;
    pthread_mutex_lock(&g.lock);
    if (g.status != LIB_AUDIO_STOPPED) {
        g.cmd = PCMD_STOP;
        pthread_cond_signal(&g.cond);
    }
    pthread_mutex_unlock(&g.lock);
}

/* ═══════════════════════════════════════════════════════════
   API - Volumen y estado
═══════════════════════════════════════════════════════════ */

void lib_audio_set_volume(int vol)
{
    if (vol < 0) vol = 0; if (vol > 100) vol = 100;
    pthread_mutex_lock(&g.lock);
    g.volume = vol;
    pthread_mutex_unlock(&g.lock);
    alsa_set_volume(vol);
}

int lib_audio_get_volume(void)
{
    pthread_mutex_lock(&g.lock);
    int v = g.volume;
    pthread_mutex_unlock(&g.lock);
    return v;
}

LibAudioStatus lib_audio_get_status(void)
{
    pthread_mutex_lock(&g.lock);
    LibAudioStatus s = g.status;
    pthread_mutex_unlock(&g.lock);
    return s;
}

int lib_audio_get_current_id(void)
{
    pthread_mutex_lock(&g.lock);
    int id = g.current_id;
    pthread_mutex_unlock(&g.lock);
    return id;
}

float lib_audio_get_position(void)
{
    pthread_mutex_lock(&g.lock);
    float p = g.position_secs;
    pthread_mutex_unlock(&g.lock);
    return p;
}

/* ═══════════════════════════════════════════════════════════
   API - Notificaciones
═══════════════════════════════════════════════════════════ */

void lib_audio_notify(NotificationEvent event)
{
    static const char *NAMES[] = {
        "notify_startup.mp3",
        "notify_autonomous.mp3",
        "notify_obstacle.mp3",
        "notify_manual.mp3",
    };

    if ((unsigned int)event >= 4) return;

    char filepath[640];
    snprintf(filepath, sizeof(filepath), "%s/%s", g.audio_dir, NAMES[event]);

    if (access(filepath, F_OK) != 0) {
        printf("[audio] Notificación '%s' no encontrada\n", NAMES[event]);
        return;
    }

    printf("[audio] Notificación: %s\n", NAMES[event]);

    int was_playing = (g.status == LIB_AUDIO_PLAYING);
    if (was_playing) lib_audio_pause();

    // Guardar volumen actual y forzar al maximo para notificaciones
    pthread_mutex_lock(&g.lock);
    int vol_original = g.volume;
    pthread_mutex_unlock(&g.lock);

    alsa_set_volume(92);  // ← forzar volumen

    int err_code;
    mpg123_handle *mh = mpg123_new(NULL, &err_code);
    if (!mh) {
        alsa_set_volume(vol_original);
        if (was_playing) lib_audio_resume();
        return;
    }

    mpg123_param(mh, MPG123_QUIET, 1, 0);

    if (mpg123_open(mh, filepath) != MPG123_OK) {
        fprintf(stderr, "[audio] No se pudo abrir notificación '%s'\n", filepath);
        mpg123_delete(mh);
        alsa_set_volume(vol_original);
        if (was_playing) lib_audio_resume();
        return;
    }

    long rate = 44100;
    int channels = 2, encoding = MPG123_ENC_SIGNED_16;
    mpg123_getformat(mh, &rate, &channels, &encoding);
    mpg123_format_none(mh);
    mpg123_format(mh, rate, channels, MPG123_ENC_SIGNED_16);

    snd_pcm_t *pcm = alsa_open(rate, channels);
    if (!pcm) {
        mpg123_close(mh);
        mpg123_delete(mh);
        alsa_set_volume(vol_original);
        if (was_playing) lib_audio_resume();
        return;
    }

    unsigned char buf[8192];
    size_t done;
    int dec_err;

    while (1) {
        dec_err = mpg123_read(mh, buf, sizeof(buf), &done);
        if (dec_err == MPG123_DONE || done == 0) break;
        if (dec_err != MPG123_OK && dec_err != MPG123_NEW_FORMAT) break;

        snd_pcm_uframes_t frames = snd_pcm_bytes_to_frames(pcm, (ssize_t)done);
        unsigned char *ptr = buf;

        while (frames > 0) {
            snd_pcm_sframes_t wr = snd_pcm_writei(pcm, ptr, frames);
            if (wr < 0) {
                wr = snd_pcm_recover(pcm, (int)wr, 0);
                if (wr < 0) break;
                continue;
            }
            ptr    += snd_pcm_frames_to_bytes(pcm, (snd_pcm_uframes_t)wr);
            frames -= (snd_pcm_uframes_t)wr;
        }
    }

    snd_pcm_drain(pcm);
    snd_pcm_close(pcm);
    mpg123_close(mh);
    mpg123_delete(mh);

    // Restaurar volumen original
    alsa_set_volume(vol_original);  // ← restaurar

    if (was_playing) lib_audio_resume();
}